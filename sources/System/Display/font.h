/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef SYSTEM_DISPLAY_FONT_H
#define SYSTEM_DISPLAY_FONT_H

#include <stdint.h>

#define FONT_CHAR_WIDTH 22
#define FONT_CHAR_HEIGHT 30
#define FONT_CHAR_COUNT 224

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t FONT[FONT_CHAR_COUNT][FONT_CHAR_HEIGHT][FONT_CHAR_WIDTH];

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_DISPLAY_FONT_H
