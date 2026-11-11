/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#pragma once
#include <stdint.h>
#define DMA2D_R2M 3U
#define HAL_MAX_DELAY 0xffffffffU
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  struct {
    uint32_t OutputOffset, Mode;
  } Init;
  struct {
    uint32_t InputOffset, InputAlpha;
  } LayerCfg[2];
} DMA2D_HandleTypeDef;
extern DMA2D_HandleTypeDef hdma2d;
int HAL_DMA2D_Init(DMA2D_HandleTypeDef *handle);
int HAL_DMA2D_ConfigLayer(DMA2D_HandleTypeDef *handle, uint32_t layer);
int HAL_DMA2D_Start(DMA2D_HandleTypeDef *handle, uint32_t color,
                    uintptr_t destination, uint32_t width, uint32_t height);
int HAL_DMA2D_BlendingStart(DMA2D_HandleTypeDef *handle, uintptr_t source,
                            uint32_t background, uintptr_t destination,
                            uint32_t width, uint32_t height);
int HAL_DMA2D_PollForTransfer(DMA2D_HandleTypeDef *handle, uint32_t timeout);
#ifdef __cplusplus
}
#endif
