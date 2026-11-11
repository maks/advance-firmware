/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "System/Display/ErrorBitmap.h"

static const uint8_t g_dummyBitmapData[] = {0x00};

const ErrorBitmap g_sdCardMissingBitmap = {
    .width = 0,
    .height = 0,
    .rowBytes = 0,
    .data = g_dummyBitmapData,
};

const ErrorBitmap g_lowBatteryBitmap = {
    .width = 0,
    .height = 0,
    .rowBytes = 0,
    .data = g_dummyBitmapData,
};

const ErrorBitmap g_deadBitmap = {
    .width = 0,
    .height = 0,
    .rowBytes = 0,
    .data = g_dummyBitmapData,
};
