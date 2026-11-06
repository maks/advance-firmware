/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "System/Display/Display.h"
#include "pixel_targets.h"
#include <stdbool.h>

#include "UIFramework/BasicDatas/TextRegionLayout.h"

// Configuration changes are transactional and do not render. The caller clears
// and redraws after a successful layout change. Definitions come from the UI;
// NULL is invalid. Before configuration the active layout is empty.
bool display_configure_text_regions(const TextRegion *regions, uint8_t count);
const TextRegionLayout *display_text_layout(void);
void display_putc_region(uint8_t region, uint8_t column, uint8_t row, char c,
                         bool invert);

bool display_configure_graphics(GraphicsLayout layout);
GraphicsLayout display_graphics_layout(void);
// Changes only when graph pixels are invalidated, never when an overlay closes.
uint32_t display_graphics_revision(void);
// Debugger-readable capture diagnostics; cycles include interrupt/preemption
// time. Underrun values count observation windows, not individual FIFO events.
typedef struct {
  uint32_t captures, last_cycles, max_cycles, last_pixels;
  uint32_t underrun_before_capture, underrun_during_capture;
} DisplayOverlayDiagnostics;
extern volatile DisplayOverlayDiagnostics gDisplayOverlayDiagnostics;
bool display_push_overlay(GraphicsRegion bounds);
void display_pop_overlay(void);
void display_discard_overlays(void);
void display_select_target(uint8_t target);
uint8_t display_target(void);
uint8_t display_overlay_depth(void);
void display_visit_pixels(GraphicsRegion bounds, bool text,
                          PixelVisitor visitor, void *context);
uint32_t display_visible_area(GraphicsRegion bounds, bool text);
void display_draw_overlay_char(uint8_t x, uint8_t y, char c, bool invert);
// Remote clients retain their own overlay backing; no framebuffer readback.
typedef enum {
  DISPLAY_OVERLAY_PUSH,
  DISPLAY_OVERLAY_POP,
  DISPLAY_OVERLAY_RESET
} DisplayOverlayEvent;
typedef void (*DisplayOverlayObserver)(DisplayOverlayEvent event,
                                       GraphicsRegion bounds);
void display_set_overlay_observer(DisplayOverlayObserver observer);

void display_set_foreground(color_t color);
void display_set_background(color_t color);
void display_clear(color_t color);
void display_set_palette_color(int idx, uint16_t rgb565_color);
void display_draw_changed();
void display_draw_screen();
void display_fill_rect(uint8_t color_index, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height);
void display_draw_mono_bitmap(uint16_t x, uint16_t y, const uint8_t *bitmap,
                              uint16_t width, uint16_t height,
                              uint16_t row_bytes, uint8_t fg_color_index,
                              uint8_t bg_color_index);
void display_draw_mono_bitmap_rgb565(uint16_t x, uint16_t y,
                                     const uint8_t *bitmap, uint16_t width,
                                     uint16_t height, uint16_t row_bytes,
                                     uint16_t fg_color565,
                                     uint16_t bg_color565);
uint32_t rgb565_to_abgr8888(uint16_t rgb565);

#ifdef __cplusplus
}
#endif

#endif
