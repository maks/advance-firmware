/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#pragma once
#include <stdint.h>
#define DISPLAY_WIDTH 720
#define DISPLAY_HEIGHT 720
extern uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];

extern uint16_t placeholder_framebuffer_layer2[DISPLAY_WIDTH * DISPLAY_HEIGHT];
