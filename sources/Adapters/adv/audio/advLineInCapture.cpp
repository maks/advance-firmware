/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "advLineInCapture.h"

#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "sai.h"
#include "tlv320aic3204.h"

extern DMA_HandleTypeDef hdma_sai1_b;

namespace {

constexpr UBaseType_t kCaptureTaskPriority = 2;
// Deep enough for the codec I2C and SAI/DMA bring up plus the trace that
// reports them, all of which run on this task.
constexpr uint16_t kCaptureTaskStackWords = 1024;
// The lock only guards a start/stop of one peripheral, so waiting longer than
// this means something else is wrong. Bounded so no caller can hang a task.
constexpr TickType_t kLockTimeout = pdMS_TO_TICKS(100);
constexpr int32_t kDefaultLineInGainDb = 0;

// Non cacheable SDRAM keeps the DMA and the CPU coherent without maintenance,
// matching the buffer the record path uses.
__attribute__((section(".SDRAM1_NC"), aligned(32)))
int16_t captureRing[AdvLineInCapture::kRingFrames * 2];

int32_t configuredLineInGainDb() {
  Config *config = Config::GetInstance();
  if (config == nullptr) {
    return kDefaultLineInGainDb;
  }
  Variable *gain = config->FindVariable(FourCC::VarRecordLineGain);
  return gain != nullptr ? gain->GetInt() : kDefaultLineInGainDb;
}

} // namespace

AdvLineInCapture *AdvLineInCapture::instance_ = nullptr;

AdvLineInCapture &AdvLineInCapture::GetInstance() {
  if (instance_ == nullptr) {
    static AdvLineInCapture capture;
    instance_ = &capture;

    static StaticSemaphore_t lockBuffer;
    capture.lock_ = xSemaphoreCreateMutexStatic(&lockBuffer);

    static StackType_t taskStack[kCaptureTaskStackWords];
    static StaticTask_t taskBuffer;
    capture.taskHandle_ =
        xTaskCreateStatic(Task, "LineIn", kCaptureTaskStackWords, nullptr,
                          kCaptureTaskPriority, taskStack, &taskBuffer);
  }
  return *instance_;
}

bool AdvLineInCapture::ownsTransfer() {
  return (instance_ != nullptr) && instance_->running_;
}

uint32_t AdvLineInCapture::writePositionFrames() const {
  if (!running_) {
    return lapBaseFrames_;
  }

  // Combine the callback's lap count with DMA's position within that lap.
  // Freeze the callback, account for an interrupt still pending, and retry if
  // DMA wraps between the flag and counter reads (DMA keeps running here).
  const uint32_t interruptMask = __get_PRIMASK();
  __disable_irq();
  const uint32_t completionFlag = __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_sai1_b);
  uint32_t pendingBefore;
  uint32_t pendingAfter;
  uint32_t remainingHalfwords;
  do {
    pendingBefore = __HAL_DMA_GET_FLAG(&hdma_sai1_b, completionFlag);
    remainingHalfwords = __HAL_DMA_GET_COUNTER(&hdma_sai1_b);
    pendingAfter = __HAL_DMA_GET_FLAG(&hdma_sai1_b, completionFlag);
  } while (pendingBefore != pendingAfter);
  const uint32_t lapBase = lapBaseFrames_;
  __set_PRIMASK(interruptMask);

  const uint32_t writtenHalfwords = (kRingFrames * 2) - remainingHalfwords;
  // At counter zero, writtenHalfwords already includes the completed lap.
  const uint32_t pendingFrames =
      pendingAfter && remainingHalfwords != 0 ? kRingFrames : 0;
  return lapBase + pendingFrames + (writtenHalfwords / 2);
}

uint32_t AdvLineInCapture::readablePositionFrames(uint32_t produced) const {
  // After an overrun, retain the newest half ring with room for DMA to advance
  // during a copy. Queries use the same position without consuming anything.
  return produced - readPositionFrames_ > kRingFrames
             ? produced - kRingFrames / 2
             : readPositionFrames_;
}

uint32_t AdvLineInCapture::availableFrames() const {
  if (!running_) {
    return 0;
  }
  const uint32_t produced = writePositionFrames();
  return produced - readablePositionFrames(produced);
}

uint32_t AdvLineInCapture::readFrames(int16_t *dst, uint32_t maxFrames) {
  if ((!running_) || (dst == nullptr) || (maxFrames == 0)) {
    return 0;
  }

  const uint32_t produced = writePositionFrames();
  readPositionFrames_ = readablePositionFrames(produced);
  const uint32_t available = produced - readPositionFrames_;

  uint32_t frames = available > maxFrames ? maxFrames : available;
  if (frames == 0) {
    return 0;
  }

  uint32_t ringIndex = readPositionFrames_ % kRingFrames;
  const uint32_t tail = kRingFrames - ringIndex;
  int16_t *out = dst;

  if (frames <= tail) {
    __builtin_memcpy(out, captureRing + ringIndex * 2,
                     frames * 2 * sizeof(int16_t));
  } else {
    __builtin_memcpy(out, captureRing + ringIndex * 2,
                     tail * 2 * sizeof(int16_t));
    __builtin_memcpy(out + tail * 2, captureRing,
                     (frames - tail) * 2 * sizeof(int16_t));
  }

  readPositionFrames_ += frames;
  return frames;
}

void AdvLineInCapture::skipFrames(uint32_t frames) {
  if (!running_) {
    return;
  }
  const uint32_t produced = writePositionFrames();
  readPositionFrames_ = readablePositionFrames(produced);
  const uint32_t available = produced - readPositionFrames_;
  if (frames > available) {
    frames = available;
  }
  readPositionFrames_ += frames;
}

void AdvLineInCapture::setCaptureRequested(bool requested) {
  requested_ = requested;
  notifyTask();
}

bool AdvLineInCapture::setRecordSessionActive(bool active) {
  recordSessionActive_ = active;

  if (!active) {
    // The record path releases the codec and the peripheral on the way out, so
    // reacquiring is left to the capture task.
    notifyTask();
    return true;
  }

  // The record path starts its own transfer as soon as this returns, so give
  // the peripheral back here rather than deferring to the capture task.
  const bool locked =
      (lock_ != nullptr) && (xSemaphoreTake(lock_, kLockTimeout) == pdTRUE);
  if (!locked) {
    Trace::Error("LINEIN", "lock timeout handing back capture");
    recordSessionActive_ = false;
    notifyTask();
    return false;
  }

  if (running_) {
    stopCapture();
  }

  xSemaphoreGive(lock_);
  return true;
}

void AdvLineInCapture::Task(void *) {
  for (;;) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (instance_ != nullptr) {
      instance_->process();
    }
  }
}

void AdvLineInCapture::notifyTask() {
  if (taskHandle_ != nullptr) {
    xTaskNotifyGive(taskHandle_);
  }
}

void AdvLineInCapture::process() {
  const bool locked =
      (lock_ != nullptr) && (xSemaphoreTake(lock_, kLockTimeout) == pdTRUE);
  if (!locked) {
    Trace::Error("LINEIN", "lock timeout in capture task");
    return;
  }

  const bool wantCapture = requested_ && !recordSessionActive_;
  if (wantCapture && !running_) {
    startCapture();
  } else if (!wantCapture && running_) {
    stopCapture();
  }

  xSemaphoreGive(lock_);
}

void AdvLineInCapture::startCapture() {
  lapBaseFrames_ = 0;
  readPositionFrames_ = 0;

  // Leave the codec alone for a record session, it configures the input itself.
  if (!recordSessionActive_) {
    // The mix always takes the line inputs, whatever the record path was last
    // told to listen to.
    tlv320_disable_mic();
    tlv320_enable_linein();
    tlv320_set_linein_analog_gain_db(configuredLineInGainDb());
  }

  const HAL_StatusTypeDef status = HAL_SAI_Receive_DMA(
      &hsai_BlockB1, reinterpret_cast<uint8_t *>(captureRing), kRingFrames * 2);
  if (status != HAL_OK) {
    Trace::Error("LINEIN", "capture DMA failed: %d", status);
    if (!recordSessionActive_) {
      tlv320_disable_linein();
    }
    return;
  }

  running_ = true;
  Trace::Log("LINEIN", "capture started");
}

void AdvLineInCapture::stopCapture() {
  running_ = false;
  HAL_SAI_DMAStop(&hsai_BlockB1);

  // A record session takes over the codec state, everything else powers the
  // converter back down.
  if (!recordSessionActive_) {
    tlv320_disable_linein();
  }
  Trace::Log("LINEIN", "capture stopped");
}

void AdvLineInCapture::RxComplete() {
  if (instance_ == nullptr) {
    return;
  }
  instance_->lapBaseFrames_ += kRingFrames;
}
