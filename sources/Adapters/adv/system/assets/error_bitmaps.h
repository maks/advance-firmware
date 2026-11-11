/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef ERROR_BITMAPS_H
#define ERROR_BITMAPS_H

#include "System/Display/ErrorBitmap.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const ErrorBitmap g_sdCardMissingBitmap;
extern const ErrorBitmap g_lowBatteryBitmap;
extern const ErrorBitmap g_deadBitmap;

#ifdef __cplusplus
}
#endif

#endif
