/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef TEXT_FONT_H
#define TEXT_FONT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// IDs are also used by remote UI clients, which supply the same font assets.
typedef enum {
  TEXT_FONT_NORMAL,
  TEXT_FONT_SMALL,
  TEXT_FONT_TINY,
  TEXT_FONT_COUNT
} TextFont;
typedef struct {
  const uint8_t *pixels;
  uint16_t count;
  uint8_t width, height;
} TextFontInfo;
extern const TextFontInfo text_fonts[TEXT_FONT_COUNT];
static inline const TextFontInfo *text_font(uint8_t id) {
  return id < TEXT_FONT_COUNT ? &text_fonts[id] : 0;
}
#ifdef __cplusplus
}
#endif
#endif
