/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef SYSTEM_DISPLAY_ERROR_BITMAP_H
#define SYSTEM_DISPLAY_ERROR_BITMAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
  const uint8_t *data;
} ErrorBitmap;

extern const ErrorBitmap g_sdCardMissingBitmap;
extern const ErrorBitmap g_lowBatteryBitmap;
extern const ErrorBitmap g_deadBitmap;

#ifdef __cplusplus
}
#endif

#endif
