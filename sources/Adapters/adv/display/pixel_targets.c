/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#include "pixel_targets.h"
#include <string.h>

GraphicsRegion pixel_intersection(GraphicsRegion a, GraphicsRegion b) {
  int32_t x = a.x > b.x ? a.x : b.x;
  int32_t y = a.y > b.y ? a.y : b.y;
  int32_t right = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
  int32_t bottom =
      a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
  return (GraphicsRegion){x, y, right > x ? right - x : 0,
                          bottom > y ? bottom - y : 0};
}

uint8_t pixel_outside(GraphicsRegion r, GraphicsRegion hole,
                      GraphicsRegion pieces[4]) {
  GraphicsRegion hit = pixel_intersection(r, hole);
  if (!hit.width || !hit.height) {
    pieces[0] = r;
    return 1;
  }
  uint8_t count = 0;
  // Disjoint strips: top and bottom span the width; sides span only the hit.
  if (hit.y > r.y)
    pieces[count++] = (GraphicsRegion){r.x, r.y, r.width, hit.y - r.y};
  int16_t bottom = hit.y + hit.height;
  if (bottom < r.y + r.height)
    pieces[count++] =
        (GraphicsRegion){r.x, bottom, r.width, r.y + r.height - bottom};
  if (hit.x > r.x)
    pieces[count++] = (GraphicsRegion){r.x, hit.y, hit.x - r.x, hit.height};
  int16_t right = hit.x + hit.width;
  if (right < r.x + r.width)
    pieces[count++] =
        (GraphicsRegion){right, hit.y, r.x + r.width - right, hit.height};
  return count;
}

bool pixel_push(PixelTargets *t, GraphicsRegion r, uint16_t *storage,
                size_t capacity) {
  if (!storage || t->depth >= GRAPHICS_OVERLAY_LIMIT)
    return false;
  GraphicsRegion clipped =
      pixel_intersection(r, (GraphicsRegion){0, 0, t->width, t->height});
  if (clipped.width != r.width || clipped.height != r.height || r.width <= 0 ||
      r.height <= 0 || (size_t)r.width * r.height > capacity)
    return false;
  // Capture current visible content, including an existing lower overlay.
  for (int16_t row = 0; row < r.height; ++row)
    memcpy(storage + row * r.width, t->front + (r.y + row) * t->width + r.x,
           r.width * sizeof(uint16_t));
  t->backing[t->depth++] = (PixelBacking){r, storage};
  return true;
}

bool pixel_pop(PixelTargets *t) {
  if (!t->depth)
    return false;
  const PixelBacking *b = &t->backing[--t->depth];
  for (int16_t row = 0; row < b->bounds.height; ++row)
    memcpy(t->front + (b->bounds.y + row) * t->width + b->bounds.x,
           b->pixels + row * b->bounds.width,
           b->bounds.width * sizeof(uint16_t));
  return true;
}

static void route_from(PixelTargets *t, uint8_t first, GraphicsRegion r,
                       PixelVisitor visit, void *ctx) {
  if (r.width <= 0 || r.height <= 0)
    return;
  for (uint8_t i = first; i < t->depth; ++i) {
    PixelBacking *b = &t->backing[i];
    GraphicsRegion hit = pixel_intersection(r, b->bounds);
    if (!hit.width || !hit.height)
      continue;
    // The first covering overlay owns the live pixels directly underneath it.
    visit(ctx, hit,
          b->pixels + (hit.y - b->bounds.y) * b->bounds.width + hit.x -
              b->bounds.x,
          b->bounds.width, false);
    GraphicsRegion pieces[4];
    uint8_t count = pixel_outside(r, hit, pieces);
    for (uint8_t p = 0; p < count; ++p)
      route_from(t, i + 1, pieces[p], visit, ctx);
    return;
  }
  visit(ctx, r, t->front + r.y * t->width + r.x, t->width, true);
}

void pixel_route(PixelTargets *t, uint8_t target, GraphicsRegion rect,
                 PixelVisitor visitor, void *context) {
  if (target > t->depth)
    return;
  GraphicsRegion clip = target ? t->backing[target - 1].bounds
                               : (GraphicsRegion){0, 0, t->width, t->height};
  route_from(t, target, pixel_intersection(rect, clip), visitor, context);
}
