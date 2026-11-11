/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef TEXT_HIGHLIGHT_H
#define TEXT_HIGHLIGHT_H

#include <stdint.h>
#include <string.h>

// Highlights join only within a region and only with identical properties.
// Bits describe exposed top-left, top-right, bottom-left and bottom-right
// corners. Concave joins stay square; no outline tracing or pixel buffer.
inline uint8_t TextHighlightCorners(const uint8_t *props, uint16_t columns,
                                    uint16_t rows, uint16_t x, uint16_t y) {
  const uint16_t at = y * columns + x;
  const uint8_t value = props[at];
  if (!(value & 0x80))
    return 0;
  const bool left = x && props[at - 1] == value;
  const bool right = x + 1 < columns && props[at + 1] == value;
  const bool top = y && props[at - columns] == value;
  const bool bottom = y + 1 < rows && props[at + columns] == value;
  return (!left && !top ? 1 : 0) | (!right && !top ? 2 : 0) |
         (!left && !bottom ? 4 : 0) | (!right && !bottom ? 8 : 0);
}

inline void TextHighlightMark(uint8_t *dirty, uint16_t at) {
  dirty[at / 8] |= 1U << (at % 8);
}

// Run before modifying the previous cache. Changing an edge also repaints its
// neighbours: former corners become straight edges on growth, and former
// interior cells acquire corners on shrink. No retained corner state needed.
inline void TextHighlightChanges(const uint8_t *chars, const uint8_t *props,
                                 const uint8_t *previous,
                                 const uint8_t *previousProps, uint16_t columns,
                                 uint16_t rows, uint8_t *dirty, uint16_t left,
                                 uint16_t top, uint16_t right,
                                 uint16_t bottom) {
  // Bounds are cell coordinates, right/bottom exclusive, within the grid.
  // Only visible cells need dirty decisions; corner checks still read their
  // neighbours in the full grid so changes just outside the bounds count.
  memset(dirty, 0, (columns * rows + 7) / 8);
  for (uint16_t y = top; y < bottom; ++y) {
    for (uint16_t x = left; x < right; ++x) {
      const uint16_t at = y * columns + x;
      if (chars[at] != previous[at] || props[at] != previousProps[at] ||
          ((props[at] & 0x80) &&
           TextHighlightCorners(props, columns, rows, x, y) !=
               TextHighlightCorners(previousProps, columns, rows, x, y)))
        TextHighlightMark(dirty, at);
    }
  }
}

inline void TextHighlightChanges(const uint8_t *chars, const uint8_t *props,
                                 const uint8_t *previous,
                                 const uint8_t *previousProps, uint16_t columns,
                                 uint16_t rows, uint8_t *dirty) {
  TextHighlightChanges(chars, props, previous, previousProps, columns, rows,
                       dirty, 0, 0, columns, rows);
}

// A fixed three-pixel radius removes three pixels from each exposed corner.
// Emit two solid spans, also suitable for the existing remote rectangle API.
template <typename Paint>
inline void TextHighlightPaint(uint8_t corners, int x, int y, int width,
                               int height, Paint paint) {
  if (width < 6 || height < 6)
    return;
  for (uint8_t corner = 0; corner < 4; ++corner) {
    if (!(corners & (1U << corner)))
      continue;
    const bool right = corner & 1, bottom = corner & 2;
    paint(right ? x + width - 2 : x, bottom ? y + height - 1 : y, 2, 1);
    paint(right ? x + width - 1 : x, bottom ? y + height - 2 : y + 1, 1, 1);
  }
}
#endif
