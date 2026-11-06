/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef TEXT_REGION_LAYOUT_H
#define TEXT_REGION_LAYOUT_H
#include "TextFont.h"
#include "TextRegionConfig.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Capacity is shared by regions, not reserved separately for each rectangle.
// Its compile-time budget and descriptor limit live in TextRegionConfig.h.
#if defined(__arm__) || defined(__thumb__)
#define TEXT_STATE_RAM __attribute__((section(".DTCMRAM"), aligned(4)))
#else
#define TEXT_STATE_RAM
#endif

// Pixel rectangle and a fixed font. Only complete glyphs fit in the local
// grid; any unused pixels at the right/bottom remain outside that grid.
typedef struct {
  uint16_t x, y, width, height;
  uint8_t font;
} TextRegion;

// A borrowed definition, returned by views. Backing arrays have static
// lifetime.
typedef struct {
  const TextRegion *regions;
  uint8_t count;
} TextLayout;

typedef struct {
  TextRegion definition;
  uint16_t offset;
  uint16_t glyph_stride;
  uint32_t pixel_offset;
  uint8_t glyph_width, glyph_height;
  uint8_t columns, rows;
} TextRegionInfo;

typedef struct {
  TextRegionInfo regions[TEXT_REGION_LIMIT];
  uint16_t cells;
  uint8_t count;
} TextRegionLayout;

static inline bool text_region_plan(TextRegionLayout *out,
                                    const TextRegion *regions, uint8_t count) {
  if (!regions || count == 0 || count > TEXT_REGION_LIMIT)
    return false;
  uint16_t cells = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const TextRegion *r = &regions[i];
    const TextFontInfo *font = text_font(r->font);
    if (!font || !r->width || !r->height)
      return false;
    uint8_t width = font->width, height = font->height;
    uint32_t right = (uint32_t)r->x + r->width;
    uint32_t bottom = (uint32_t)r->y + r->height;
    if (r->x >= 720 || r->y >= 720 || right > 720 || bottom > 720)
      return false;
    for (uint8_t j = 0; j < i; ++j) {
      const TextRegionInfo *p = &out->regions[j];
      if (r->x < p->definition.x + p->definition.width &&
          right > p->definition.x &&
          r->y < p->definition.y + p->definition.height &&
          bottom > p->definition.y)
        return false;
    }
    uint16_t columns = r->width / width, rows = r->height / height;
    if (!columns || !rows || columns > UINT8_MAX || rows > UINT8_MAX)
      return false;
    uint16_t size = columns * rows;
    if (size > TEXT_REGION_CAPACITY - cells)
      return false;
    TextRegionInfo *p = &out->regions[i];
    p->definition.x = r->x;
    p->definition.y = r->y;
    p->definition.width = r->width;
    p->definition.height = r->height;
    p->columns = columns;
    p->rows = rows;
    p->definition.font = r->font;
    p->offset = cells;
    p->glyph_width = width;
    p->glyph_height = height;
    p->glyph_stride = width * height;
    p->pixel_offset = (uint32_t)r->y * 720 + r->x;
    cells += size;
  }
  out->cells = cells;
  out->count = count;
  return true;
}

// Submit complete cells in local coordinates. Invalid starts are ignored;
// characters past the right edge are ignored without partial glyph rendering.
static inline uint16_t text_region_write(const TextRegionInfo *region,
                                         uint8_t *chars, uint8_t *properties,
                                         int32_t x, int32_t y, const char *text,
                                         uint8_t property) {
  if (!text || x < 0 || y < 0 || y >= region->rows || x >= region->columns)
    return 0;
  uint16_t index = region->offset + y * region->columns + x;
  uint16_t count = 0;
  while (x++ < region->columns && *text) {
    chars[index] = (uint8_t)*text++;
    properties[index++] = property;
    ++count;
  }
  return count;
}
// Style complete cells without changing their characters, including blank
// separators. Bounds are local and right/bottom are exclusive. Rebuild normal
// text before applying a moved selection so its previous styling is restored.
static inline void text_region_style(const TextRegionInfo *region,
                                     uint8_t *properties, int32_t left,
                                     int32_t top, int32_t right, int32_t bottom,
                                     uint8_t property) {
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  if (right > region->columns)
    right = region->columns;
  if (bottom > region->rows)
    bottom = region->rows;
  if (left >= right || top >= bottom)
    return;
  for (int32_t y = top; y < bottom; ++y)
    memset(properties + region->offset + y * region->columns + left, property,
           right - left);
}
#endif
