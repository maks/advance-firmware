/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef PIXEL_TARGETS_H
#define PIXEL_TARGETS_H
#include "UIFramework/BasicDatas/GraphicsRegion.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  GraphicsRegion bounds;
  uint16_t *pixels;
} PixelBacking;
typedef struct {
  uint16_t *front;
  int16_t width, height;
  uint8_t depth;
  PixelBacking backing[GRAPHICS_OVERLAY_LIMIT];
} PixelTargets;
typedef void (*PixelVisitor)(void *, GraphicsRegion, uint16_t *, uint16_t,
                             bool);
GraphicsRegion pixel_intersection(GraphicsRegion a, GraphicsRegion b);
uint8_t pixel_outside(GraphicsRegion rect, GraphicsRegion hole,
                      GraphicsRegion pieces[4]);
bool pixel_push(PixelTargets *targets, GraphicsRegion bounds, uint16_t *storage,
                size_t capacity);
bool pixel_pop(PixelTargets *targets);
void pixel_route(PixelTargets *targets, uint8_t target, GraphicsRegion rect,
                 PixelVisitor visitor, void *context);
#ifdef __cplusplus
}
#endif
#endif
