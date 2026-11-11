/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef GRAPHICS_REGION_H
#define GRAPHICS_REGION_H
#include <stdint.h>
#define GRAPHICS_REGION_LIMIT 4
#define GRAPHICS_OVERLAY_LIMIT 2
// Bounds only: no pixel array or retained drawing commands belong to a region.
typedef struct {
  int16_t x, y, width, height;
} GraphicsRegion;
typedef struct {
  const GraphicsRegion *regions;
  uint8_t count;
} GraphicsLayout;
#endif
