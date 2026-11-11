/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * Platform backend interface for the shared PicoTracker display renderer.
 */

#ifndef SYSTEM_DISPLAY_DISPLAY_BACKEND_H
#define SYSTEM_DISPLAY_DISPLAY_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t *display_backend_get_framebuffer(void);

void display_backend_draw_region(uint8_t x, uint8_t y, uint8_t width,
                                 uint8_t height, uint32_t bg_color,
                                 uint32_t fg_color,
                                 const uint8_t *screen_chars);

void display_backend_mark_dirty(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_DISPLAY_DISPLAY_BACKEND_H
