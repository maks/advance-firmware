/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef TEXT_LAYOUT_GAPS_H
#define TEXT_LAYOUT_GAPS_H

#include "GraphicsRegion.h"
#include "TextRegionLayout.h"
#include <algorithm>

// Paint the complement of an installed layout as non-overlapping rectangles.
// Only used for background colour changes, never for ordinary text updates.
// Scratch space is bounded by layout limits; no retained regions or pixel map.
template <typename Paint>
void PaintTextLayoutGaps(const TextRegionLayout &text, GraphicsLayout graphics,
                         int16_t width, int16_t height, Paint paint) {
  constexpr uint8_t limit = TEXT_REGION_LIMIT + GRAPHICS_REGION_LIMIT;
  GraphicsRegion occupied[limit];
  uint8_t count = 0;
  auto add = [&](int x, int y, int w, int h) {
    const int left = std::max(0, x), top = std::max(0, y);
    const int right = std::min<int>(width, x + w);
    const int bottom = std::min<int>(height, y + h);
    if (right > left && bottom > top)
      occupied[count++] = {int16_t(left), int16_t(top), int16_t(right - left),
                           int16_t(bottom - top)};
  };
  for (uint8_t i = 0; i < text.count; ++i) {
    const TextRegionInfo &r = text.regions[i];
    // Include leftover strips inside a region that cannot fit a whole glyph.
    add(r.definition.x, r.definition.y, r.columns * r.glyph_width,
        r.rows * r.glyph_height);
  }
  for (uint8_t i = 0; i < graphics.count; ++i) {
    const GraphicsRegion &r = graphics.regions[i];
    add(r.x, r.y, r.width, r.height);
  }
  for (int16_t top = 0; top < height;) {
    int16_t bottom = height;
    struct Interval {
      int16_t left, right;
    };
    Interval intervals[limit];
    uint8_t used = 0;
    for (uint8_t i = 0; i < count; ++i) {
      const GraphicsRegion &r = occupied[i];
      const int16_t end = r.y + r.height;
      if (r.y > top)
        bottom = std::min(bottom, r.y);
      if (end > top)
        bottom = std::min(bottom, end);
      if (r.y <= top && end > top) {
        // Keep intervals sorted by their left edge, then merge overlaps below.
        uint8_t at = used++;
        while (at && intervals[at - 1].left > r.x) {
          intervals[at] = intervals[at - 1];
          --at;
        }
        intervals[at] = {r.x, int16_t(r.x + r.width)};
      }
    }
    int16_t left = 0;
    for (uint8_t i = 0; i < used; ++i) {
      if (intervals[i].left > left)
        paint(GraphicsRegion{left, top, int16_t(intervals[i].left - left),
                             int16_t(bottom - top)});
      left = std::max(left, intervals[i].right);
    }
    if (left < width)
      paint(GraphicsRegion{left, top, int16_t(width - left),
                           int16_t(bottom - top)});
    top = bottom;
  }
}
#endif
