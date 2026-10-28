/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2023 STMicroelectronics.
 * Copyright (c) 2025 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

/* Includes ------------------------------------------------------------------*/
#include "sd_diskio.h"
#include "FreeRTOS.h"
#include "System/Profiler/StreamingPerf.h"
#include "main.h"
#include "semphr.h"
#include "task.h"
#include <string.h>

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
#ifndef BLOCKSIZE
#define BLOCKSIZE 512
#endif

// SDMMC1 IDMA staging must live in DMA-accessible SRAM, never DTCM.
// AXI SRAM is non-cacheable in the MPU configuration. Every byte is filled
// before use, so this NOLOAD buffer needs no startup initialization.
#define STAGING_SECTORS 8U
static uint8_t scratch[BLOCKSIZE * STAGING_SECTORS]
    __attribute__((section(".DATA_RAM"), aligned(32)));
static StaticSemaphore_t completionStorage __attribute__((section(".RAM_D3")));
static StaticSemaphore_t ioMutexStorage __attribute__((section(".RAM_D3")));
static SemaphoreHandle_t completion;
static SemaphoreHandle_t ioMutex;
static volatile uint32_t transferStatus;
static volatile DSTATUS Stat = STA_NOINIT;

static BaseType_t SD_scheduler_running(void) {
  return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static BaseType_t SD_lock(void) {
  if (!SD_scheduler_running()) {
    return pdTRUE;
  }
  taskENTER_CRITICAL();
  if (completion == NULL) {
    completion = xSemaphoreCreateBinaryStatic(&completionStorage);
    ioMutex = xSemaphoreCreateMutexStatic(&ioMutexStorage);
    // Boot mounts the card before FreeRTOS starts, when kernel IRQs may be
    // masked. Use the RTOS-safe IRQ priority only once the scheduler runs.
    HAL_NVIC_SetPriority(SDMMC1_IRQn,
                         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
  }
  taskEXIT_CRITICAL();
  return ioMutex != NULL &&
         xSemaphoreTake(ioMutex, pdMS_TO_TICKS(SD_TIMEOUT)) == pdTRUE;
}

static void SD_unlock(void) {
  if (SD_scheduler_running()) {
    xSemaphoreGive(ioMutex);
  }
}

static void SD_signal(uint32_t status) {
  transferStatus = status;
  if (completion != NULL &&
      xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(completion, &woken);
    portYIELD_FROM_ISR(woken);
  }
}

static void SD_begin_transfer(void) {
  if (completion != NULL) {
    xSemaphoreTake(completion, 0);
  }
  transferStatus = 0;
}

static BaseType_t SD_wait_transfer(void) {
  const uint32_t start = DWT->CYCCNT;
  if (SD_scheduler_running() && completion != NULL) {
    xSemaphoreTake(completion, pdMS_TO_TICKS(SD_TIMEOUT));
  } else {
    // Mount/boot before the scheduler is the only polling fallback.
    const uint32_t started = HAL_GetTick();
    while (transferStatus == 0 && HAL_GetTick() - started < SD_TIMEOUT) {
    }
  }
  RecordStreamingPerf(&streamingPerf.sdWait, DWT->CYCCNT - start);
  if (transferStatus == 1) {
    return pdTRUE;
  }
  if (transferStatus == 0) {
    streamingPerf.sdTimeouts = streamingPerf.sdTimeouts + 1;
  }
  // Stop IDMA and clear pending IRQs before a later caller reuses its buffer.
  HAL_SD_Abort(&sdmmc_handle);
  HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
  streamingPerf.sdErrors = streamingPerf.sdErrors + 1;
  return pdFALSE;
}

static BaseType_t SD_dma_address(const void *buffer) {
  const uintptr_t address = (uintptr_t)buffer;
  return (address & 31U) == 0 &&
         ((address >= 0x24000000U && address < 0x24080000U) ||
          (address >= 0xC0000000U && address < 0xC4000000U));
}

static uint8_t SD_is_card_present(void);
/* Private function prototypes -----------------------------------------------*/
static int SD_check_status_with_timeout(uint32_t);
static DSTATUS SD_check_status(BYTE);
static DSTATUS SD_DMA_initialize(BYTE);
static DSTATUS SD_DMA_status(BYTE);
static DRESULT SD_DMA_read(BYTE, BYTE *, LBA_t, UINT);
static DRESULT SD_DMA_write(BYTE, const BYTE *, LBA_t, UINT);
static DRESULT SD_DMA_ioctl(BYTE, BYTE, void *);

const Diskio_drvTypeDef SD_DMA_Driver = {
    SD_DMA_initialize, SD_DMA_status, SD_DMA_read, SD_DMA_write, SD_DMA_ioctl};

/* Private functions ---------------------------------------------------------*/
/**
 * @brief  Check the status of the sd card
 * @param  lun : not used
 * @retval DSTATUS: return 0 if the sd card is ready and -1 otherwise
 */
static int SD_check_status_with_timeout(uint32_t timeout) {
  if (!SD_is_card_present()) {
    return -1;
  }
  uint32_t timer = HAL_GetTick();
  /* block until SDIO IP is ready again or a timeout occur */
  while (HAL_GetTick() - timer < timeout) {
    if (HAL_SD_GetCardState(&sdmmc_handle) == HAL_SD_CARD_TRANSFER) {
      return 0;
    }
    if (SD_scheduler_running()) {
      vTaskDelay(1);
    }
  }

  return -1;
}

/**
 * @brief  Check the status of the sd card
 * @param  lun : not used
 * @retval DSTATUS: return 0 if the sd card is ready and 1 otherwise
 */
static DSTATUS SD_check_status(BYTE lun) {
  Stat = STA_NOINIT;

  if (SD_is_card_present() &&
      (HAL_SD_GetCardState(&sdmmc_handle) == HAL_SD_CARD_TRANSFER)) {
    Stat &= ~STA_NOINIT;
  }

  return Stat;
}

/**
 * @brief  Initialize the SD Diskio lowlevel driver
 * @param  lun : not used
 * @retval DSTATUS: return 0 on Success STA_NOINIT otherwise
 */
static DSTATUS SD_DMA_initialize(BYTE lun) {
#if (ENABLE_SD_INIT == 1)
  sdmmc_sd_init();
#endif
  Stat = SD_check_status(lun);
  return Stat;
}

/**
 * @brief  Get the Disk Status
 * @param  lun : not used
 * @retval DSTATUS: return 0 if the sd card is ready and 1 otherwise status
 */
static DSTATUS SD_DMA_status(BYTE lun) { return SD_check_status(lun); }

/**
 * @brief  Read data from sd card into a buffer
 * @param  lun : not used
 * @param  *buff: Data buffer to store read data
 * @param  sector: Sector address (LBA)
 * @param  count: Number of sectors to read (1..128)
 * @retval DRESULT: return RES_OK otherwise
 */
static DRESULT SD_DMA_read(BYTE lun, BYTE *buff, LBA_t sector, UINT count) {
  (void)lun;
  if (count == 0 || !SD_lock()) {
    return RES_ERROR;
  }
  DRESULT result = RES_ERROR;
  while (count != 0) {
    if (SD_check_status_with_timeout(SD_TIMEOUT) < 0) {
      break;
    }
    const BaseType_t direct = SD_dma_address(buff);
    const UINT blocks =
        direct ? count : (count < STAGING_SECTORS ? count : STAGING_SECTORS);
    uint8_t *target = direct ? buff : scratch;
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
    // Clean before invalidating: dirty SDRAM lines must not later evict over
    // data written by DMA. Addresses and sizes are cache-line aligned.
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)target, blocks * BLOCKSIZE);
#endif
    SD_begin_transfer();
    if (HAL_SD_ReadBlocks_DMA(&sdmmc_handle, target, sector, blocks) !=
        HAL_OK) {
      HAL_SD_Abort(&sdmmc_handle);
      streamingPerf.sdErrors = streamingPerf.sdErrors + 1;
      break;
    }
    streamingPerf.sdReadTransfers = streamingPerf.sdReadTransfers + 1;
    streamingPerf.sdReadSectors = streamingPerf.sdReadSectors + blocks;
    if (!direct) {
      streamingPerf.sdBounceTransfers = streamingPerf.sdBounceTransfers + 1;
    }
    if (!SD_wait_transfer() || SD_check_status_with_timeout(SD_TIMEOUT) < 0) {
      break;
    }
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
    SCB_InvalidateDCache_by_Addr((uint32_t *)target, blocks * BLOCKSIZE);
#endif
    if (!direct) {
      memcpy(buff, scratch, blocks * BLOCKSIZE);
    }
    buff += blocks * BLOCKSIZE;
    sector += blocks;
    count -= blocks;
    if (count == 0) {
      result = RES_OK;
    }
  }
  SD_unlock();
  return result;
}

static DRESULT SD_DMA_write(BYTE lun, const BYTE *buff, LBA_t sector,
                            UINT count) {
  (void)lun;
  if (count == 0 || !SD_lock()) {
    return RES_ERROR;
  }
  DRESULT result = RES_ERROR;
  while (count != 0) {
    if (SD_check_status_with_timeout(SD_TIMEOUT) < 0) {
      break;
    }
    const BaseType_t direct = SD_dma_address(buff);
    const UINT blocks =
        direct ? count : (count < STAGING_SECTORS ? count : STAGING_SECTORS);
    const uint8_t *source = direct ? buff : scratch;
    if (!direct) {
      memcpy(scratch, buff, blocks * BLOCKSIZE);
    }
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
    SCB_CleanDCache_by_Addr((uint32_t *)source, blocks * BLOCKSIZE);
#endif
    SD_begin_transfer();
    if (HAL_SD_WriteBlocks_DMA(&sdmmc_handle, (uint8_t *)source, sector,
                               blocks) != HAL_OK) {
      HAL_SD_Abort(&sdmmc_handle);
      streamingPerf.sdErrors = streamingPerf.sdErrors + 1;
      break;
    }
    if (!SD_wait_transfer() || SD_check_status_with_timeout(SD_TIMEOUT) < 0) {
      break;
    }
    buff += blocks * BLOCKSIZE;
    sector += blocks;
    count -= blocks;
    if (count == 0) {
      result = RES_OK;
    }
  }
  SD_unlock();
  return result;
}

/**
 * @brief  I/O control operation
 * @param  lun : not used
 * @param  cmd: Control code
 * @param  *buff: Buffer to send/receive control data
 * @retval DRESULT: return RES_OK otherwise
 */
static DRESULT SD_DMA_ioctl(BYTE lun, BYTE cmd, void *buff) {
  DRESULT res = RES_ERROR;
  HAL_SD_CardInfoTypeDef CardInfo;

  if (Stat & STA_NOINIT)
    return RES_NOTRDY;

  switch (cmd) {
  /* Make sure that no pending write process */
  case CTRL_SYNC:
    res = RES_OK;
    break;

  /* Get number of sectors on the disk (DWORD) */
  case GET_SECTOR_COUNT:
    HAL_SD_GetCardInfo(&sdmmc_handle, &CardInfo);
    *(DWORD *)buff = CardInfo.LogBlockNbr;
    res = RES_OK;
    break;

  /* Get R/W sector size (WORD) */
  case GET_SECTOR_SIZE:
    HAL_SD_GetCardInfo(&sdmmc_handle, &CardInfo);
    *(WORD *)buff = CardInfo.LogBlockSize;
    res = RES_OK;
    break;

  /* Get erase block size in unit of sector (DWORD) */
  case GET_BLOCK_SIZE:
    HAL_SD_GetCardInfo(&sdmmc_handle, &CardInfo);
    *(DWORD *)buff = CardInfo.LogBlockSize / BLOCKSIZE;
    res = RES_OK;
    break;

  default:
    res = RES_PARERR;
  }

  return res;
}

static uint8_t SD_is_card_present(void) {
  return (HAL_GPIO_ReadPin(SD_DET_GPIO_Port, SD_DET_Pin) == GPIO_PIN_RESET)
             ? 1U
             : 0U;
}

/**
 * @brief Tx Transfer completed callbacks
 * @param hsd: SD handle
 * @retval None
 */

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd) {
  if (hsd == &sdmmc_handle) {
    SD_signal(1);
  }
}

/**
 * @brief Rx Transfer completed callbacks
 * @param hsd: SD handle
 * @retval None
 */

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd) {
  if (hsd == &sdmmc_handle) {
    SD_signal(1);
  }
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd) {
  if (hsd == &sdmmc_handle) {
    SD_signal(2);
  }
}
