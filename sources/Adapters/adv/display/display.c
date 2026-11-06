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
#include "display.h"
#include "dma2d.h"
#include "ltdc.h"
#include "usart.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static color_t screen_bg_color = COLOR_BG;
static color_t screen_fg_color = COLOR_NORMAL;
static uint8_t screen[TEXT_REGION_CAPACITY] TEXT_STATE_RAM;
static uint8_t colors[TEXT_REGION_CAPACITY] TEXT_STATE_RAM;

// Using a bit array in order to save memory, there is a slight performance
// hit in doing so vs a bool array
static uint8_t changed[(TEXT_REGION_CAPACITY + 7) / 8] TEXT_STATE_RAM;
static TextRegionLayout layout TEXT_STATE_RAM;
static bool layout_initialized;
static bool layout_needs_clear;
static uint8_t dirty_regions;
#define SetBit(A, k) (A[(k) / 8] |= (1 << ((k) % 8)))
#define ClearBit(A, k) (A[(k) / 8] &= ~(1 << ((k) % 8)))
#define TestBit(A, k) (A[(k) / 8] & (1 << ((k) % 8)))

// The existing spare framebuffer is backing storage, never another scanout
// layer.
#ifdef ADV
__attribute__((section(".FRAMEBUFFER"), aligned(32)))
#endif
static uint16_t countdown_backing[DISPLAY_WIDTH * 90];
static PixelTargets targets = {
    .front = framebuffer, .width = DISPLAY_WIDTH, .height = DISPLAY_HEIGHT};
static uint8_t active_target;
static GraphicsRegion graphics_regions[GRAPHICS_REGION_LIMIT];
static uint8_t graphics_count;
static uint32_t graphics_revision;
// Prepared once per layout: cells intersecting graphics are not text cells.
// One ownership bit per cell, using the same bitmap helpers as dirty text.
static uint8_t graphics_cells[(TEXT_REGION_CAPACITY + 7) / 8] TEXT_STATE_RAM;
static DisplayOverlayObserver overlay_observer;

void display_set_overlay_observer(DisplayOverlayObserver observer) {
  overlay_observer = observer;
}
void display_select_target(uint8_t target) { active_target = target; }
uint8_t display_target(void) { return active_target; }
uint8_t display_overlay_depth(void) { return targets.depth; }
void display_discard_overlays(void) {
  if (targets.depth && overlay_observer)
    overlay_observer(DISPLAY_OVERLAY_RESET, (GraphicsRegion){0, 0, 0, 0});
  targets.depth = 0;
  active_target = 0;
}
volatile DisplayOverlayDiagnostics gDisplayOverlayDiagnostics;
bool display_push_overlay(GraphicsRegion bounds) {
#ifdef ADV
  // No logging in the render path. Sample the sticky FIFO flag around the
  // copy so an earlier display fault is not mistaken for a capture fault.
  if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  }
  if (LTDC->ISR & LTDC_ISR_FUIF) {
    ++gDisplayOverlayDiagnostics.underrun_before_capture;
    LTDC->ICR = LTDC_ICR_CFUIF;
  }
  uint32_t start = DWT->CYCCNT;
#endif
  bool captured = pixel_push(
      &targets, bounds,
      targets.depth ? countdown_backing : placeholder_framebuffer_layer2,
      targets.depth ? DISPLAY_WIDTH * 90 : DISPLAY_WIDTH * DISPLAY_HEIGHT);
#ifdef ADV
  uint32_t elapsed = DWT->CYCCNT - start;
  if (LTDC->ISR & LTDC_ISR_FUIF) {
    ++gDisplayOverlayDiagnostics.underrun_during_capture;
    LTDC->ICR = LTDC_ICR_CFUIF;
  }
  if (captured) {
    ++gDisplayOverlayDiagnostics.captures;
    gDisplayOverlayDiagnostics.last_pixels =
        (uint32_t)bounds.width * bounds.height;
    gDisplayOverlayDiagnostics.last_cycles = elapsed;
    if (elapsed > gDisplayOverlayDiagnostics.max_cycles)
      gDisplayOverlayDiagnostics.max_cycles = elapsed;
  }
#endif
  if (captured && overlay_observer)
    overlay_observer(DISPLAY_OVERLAY_PUSH, bounds);
  return captured;
}
void display_pop_overlay(void) {
  if (!targets.depth)
    return;
  GraphicsRegion bounds = targets.backing[targets.depth - 1].bounds;
  pixel_pop(&targets);
  active_target = 0;
  if (overlay_observer)
    overlay_observer(DISPLAY_OVERLAY_POP, bounds);
}
uint32_t display_graphics_revision(void) { return graphics_revision; }
GraphicsLayout display_graphics_layout(void) {
  return (GraphicsLayout){graphics_regions, graphics_count};
}

// Graphics own their rectangles. An intersecting base glyph is ignored whole;
// only overlay backing needs fragments to preserve live hidden updates.
static void route_text(uint8_t target, GraphicsRegion rect,
                       PixelVisitor visitor, void *context) {
  if (!target) {
    for (uint8_t i = 0; i < graphics_count; ++i) {
      GraphicsRegion hit = pixel_intersection(rect, graphics_regions[i]);
      if (hit.width && hit.height)
        return;
    }
  }
  pixel_route(&targets, target, rect, visitor, context);
}
void display_visit_pixels(GraphicsRegion bounds, bool text,
                          PixelVisitor visitor, void *context) {
  if (text)
    route_text(active_target, bounds, visitor, context);
  else
    pixel_route(&targets, active_target, bounds, visitor, context);
}
static void count_visible(void *context, GraphicsRegion rect, uint16_t *pixels,
                          uint16_t stride, bool visible) {
  (void)pixels;
  (void)stride;
  if (visible)
    *(uint32_t *)context += (uint32_t)rect.width * rect.height;
}
uint32_t display_visible_area(GraphicsRegion rect, bool text) {
  if (!targets.depth && (!text || !graphics_count))
    return (uint32_t)rect.width * rect.height;
  uint32_t area = 0;
  if (text)
    route_text(active_target, rect, count_visible, &area);
  else
    pixel_route(&targets, active_target, rect, count_visible, &area);
  return area;
}

bool display_configure_graphics(GraphicsLayout config) {
  if (config.count > GRAPHICS_REGION_LIMIT || (config.count && !config.regions))
    return false;
  for (uint8_t i = 0; i < config.count; ++i) {
    GraphicsRegion r = config.regions[i];
    if (r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
        r.x + r.width > DISPLAY_WIDTH || r.y + r.height > DISPLAY_HEIGHT)
      return false;
    for (uint8_t j = 0; j < i; ++j) {
      GraphicsRegion hit = pixel_intersection(r, config.regions[j]);
      if (hit.width && hit.height)
        return false;
    }
  }
  if (graphics_count == config.count &&
      (!config.count || !memcmp(graphics_regions, config.regions,
                                config.count * sizeof(GraphicsRegion))))
    return true;
  // Erase pixels no longer owned by a graph, including margins outside the
  // text grids. Existing text is then repainted from its retained cells.
  for (uint8_t i = 0; i < graphics_count; ++i) {
    GraphicsRegion old = graphics_regions[i];
    display_fill_rect(COLOR_BG, old.x, old.y, old.width, old.height);
  }
  ++graphics_revision;
  graphics_count = config.count;
  if (config.count)
    memcpy(graphics_regions, config.regions,
           config.count * sizeof(GraphicsRegion));
  memset(graphics_cells, 0, sizeof(graphics_cells));
  for (uint8_t r = 0; r < layout.count; ++r) {
    const TextRegionInfo *t = &layout.regions[r];
    for (uint8_t y = 0; y < t->rows; ++y)
      for (uint8_t x = 0; x < t->columns; ++x) {
        uint16_t index = t->offset + y * t->columns + x;
        GraphicsRegion cell = {t->definition.x + x * t->glyph_width,
                               t->definition.y + y * t->glyph_height,
                               t->glyph_width, t->glyph_height};
        for (uint8_t g = 0; g < graphics_count; ++g) {
          GraphicsRegion hit = pixel_intersection(cell, graphics_regions[g]);
          if (hit.width && hit.height) {
            SetBit(graphics_cells, index);
            break;
          }
        }
      }
  }
  // A removed graph exposes cached text, which may otherwise look unchanged.
  memset(changed, 0xff, (layout.cells + 7) / 8);
  dirty_regions = (1U << layout.count) - 1;
  return true;
}

uint32_t rgb565_to_abgr8888(uint16_t rgb565) {
  uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
  uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
  uint8_t b = (rgb565 & 0x1F) << 3;
  return 0xFF000000 | (b << 16) | (g << 8) | r;
}

// Default palette, can be redefined
// Colors are defined as RGB565 in pT (for the moment) but we use ABGR8888 on
// the display driver
static uint32_t palette[16] = {
    0xFF000000, // 0x0000
    0xFF65CC92, // 0x49E5
    0xFF2E92B2, // 0xB926
    0xFF71C5C7, // 0xE371
    0xFF73F19C, // 0x9CF3
    0xFF2494A3, // 0xA324
    0xFF46C3EC, // 0xEC46
    0xFF0DF7F7, // 0xF70D
    0xFFFFFFFF, // 0xFFFF
    0xFF269019, // 0x1926
    0xFF49A12A, // 0x2A49
    0xFF438244, // 0x4443
    0xFF6496A6, // 0xA664
    0xFFB00200, // 0x02B0
    0xFF1E9635, // 0x351E
    0xFFFD6DB6, // 0xB6FD
};

static inline uint16_t abgr8888_to_bgr565(uint32_t color) {
  uint16_t b = (uint16_t)((color >> 19) & 0x1F);
  uint16_t g = (uint16_t)((color >> 10) & 0x3F);
  uint16_t r = (uint16_t)((color >> 3) & 0x1F);
  return (uint16_t)((b << 11) | (g << 5) | r);
}

bool display_configure_text_regions(const TextRegion *regions, uint8_t count) {
  TextRegionLayout next = {0};
  if (!text_region_plan(&next, regions, count))
    return false;
  // Do not disturb the text cache when a screen reapplies its current layout.
  if (layout_initialized && memcmp(&layout, &next, sizeof(next)) == 0)
    return true;
  graphics_count = 0;
  // The ownership bitmap is also in NOLOAD DTCM; clear it before text draws.
  memset(graphics_cells, 0, sizeof(graphics_cells));
  layout = next;
  layout_initialized = true;
  layout_needs_clear = true;
  dirty_regions = 0;
  // DTCMRAM is NOLOAD: every live byte is initialized explicitly.
  memset(screen, 0, layout.cells);
  memset(colors, 0, layout.cells);
  memset(changed, 0, sizeof(changed));
  return true;
}

const TextRegionLayout *display_text_layout(void) {
  if (!layout_initialized) {
    // DTCMRAM is NOLOAD. Start empty until the application supplies a layout;
    // early pixel clears must not invent a screen layout in the driver.
    memset(&layout, 0, sizeof(layout));
    layout_initialized = true;
  }
  return &layout;
}

void display_putc_region(uint8_t region, uint8_t column, uint8_t row, char c,
                         bool invert) {
  if (region >= layout.count)
    return;
  const TextRegionInfo *r = &layout.regions[region];
  if (column >= r->columns || row >= r->rows)
    return;
  uint16_t idx = r->offset + row * r->columns + column;
  // Treat incoming character bytes as unsigned so custom glyphs (0x80-0xFF)
  // are not dropped on toolchains where plain char is signed.
  uint8_t uc = (uint8_t)c;
  if (uc >= 32) {
    screen[idx] = uc - 32;
    SetBit(changed, idx);
    dirty_regions |= 1U << region;
    if (invert) {
      colors[idx] = ((screen_bg_color & 0xf) << 4) | (screen_fg_color & 0xf);
    } else {
      colors[idx] = ((screen_fg_color & 0xf) << 4) | (screen_bg_color & 0xf);
    }
  }
}

void display_set_foreground(color_t color) { screen_fg_color = color; }

void display_set_background(color_t color) { screen_bg_color = color; }

static void clear_pixels(color_t color) {
  ++graphics_revision;
  // A clear is a single DMA2D register-to-memory fill. No glyph staging,
  // framebuffer snapshot, or CPU pixel loop is needed, including layout gaps.
  uint32_t mode = hdma2d.Init.Mode;
  uint32_t offset = hdma2d.Init.OutputOffset;
  hdma2d.Init.Mode = DMA2D_R2M;
  hdma2d.Init.OutputOffset = 0;
  HAL_DMA2D_Init(&hdma2d);
  // HAL expects the 8888 color here and converts it to the output RGB565
  // format.
  HAL_DMA2D_Start(&hdma2d, palette[color], (uintptr_t)framebuffer,
                  DISPLAY_WIDTH, DISPLAY_HEIGHT);
  HAL_DMA2D_PollForTransfer(&hdma2d, HAL_MAX_DELAY);
  hdma2d.Init.Mode = mode;
  hdma2d.Init.OutputOffset = offset;
  HAL_DMA2D_Init(&hdma2d);
  layout_needs_clear = false;
}

void display_clear(color_t color) {
  display_discard_overlays();
  display_text_layout();
  memset(screen, 0, layout.cells);
  memset(colors, color, layout.cells);
  memset(changed, 0, sizeof(changed));
  dirty_regions = 0;
  clear_pixels(color);
}

void display_set_palette_color(int idx, uint16_t rgb565_color) {
  palette[idx] = rgb565_to_abgr8888(rgb565_color);
}

typedef struct {
  const uint8_t *glyph;
  GraphicsRegion bounds;
  uint32_t bg, fg;
} GlyphPaint;
// HAL fields reflect the last programmed state. Reuse it across whole glyphs
// and only reprogram when a fragment changes stride or foreground color.
static void configure_glyph(uint32_t output_offset, uint32_t input_offset,
                            uint32_t foreground) {
  if (hdma2d.Init.OutputOffset != output_offset) {
    hdma2d.Init.OutputOffset = output_offset;
    HAL_DMA2D_Init(&hdma2d);
  }
  if (hdma2d.LayerCfg[1].InputOffset != input_offset ||
      hdma2d.LayerCfg[1].InputAlpha != foreground) {
    hdma2d.LayerCfg[1].InputOffset = input_offset;
    hdma2d.LayerCfg[1].InputAlpha = foreground;
    HAL_DMA2D_ConfigLayer(&hdma2d, 1);
  }
}

static bool overlays_intersect(GraphicsRegion rect) {
  for (uint8_t i = 0; i < targets.depth; ++i) {
    GraphicsRegion hit = pixel_intersection(rect, targets.backing[i].bounds);
    if (hit.width && hit.height)
      return true;
  }
  return false;
}

static void paint_glyph(void *context, GraphicsRegion r, uint16_t *dest,
                        uint16_t stride, bool visible) {
  GlyphPaint *g = context;
  configure_glyph(stride - r.width, g->bounds.width - r.width, g->fg);
  const uint8_t *source =
      g->glyph + (r.y - g->bounds.y) * g->bounds.width + r.x - g->bounds.x;
  HAL_DMA2D_BlendingStart(&hdma2d, (uintptr_t)source, g->bg, (uintptr_t)dest,
                          r.width, r.height);
  HAL_DMA2D_PollForTransfer(&hdma2d, HAL_MAX_DELAY);
  (void)visible; // Remote text is sent as glyph commands, never read back here.
}
void display_draw_overlay_char(uint8_t x, uint8_t y, char c, bool invert) {
  if (!active_target || x >= 32 || y >= 24 || (uint8_t)c < 32)
    return;
  GraphicsRegion r = {8 + x * CHAR_WIDTH, y * CHAR_HEIGHT, CHAR_WIDTH,
                      CHAR_HEIGHT};
  const TextFontInfo *font = text_font(TEXT_FONT_NORMAL);
  GlyphPaint g = {font->pixels + ((uint8_t)c - 32) * CHAR_WIDTH * CHAR_HEIGHT,
                  r, palette[invert ? screen_fg_color : screen_bg_color],
                  palette[invert ? screen_bg_color : screen_fg_color]};
  route_text(active_target, r, paint_glyph, &g);
}

static void draw_cells(const TextRegionInfo *r, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height) {
  // Each region shares the same FG and BG color, so we define at the start and
  // reconfigure the FG layer for the given text color.
  uint16_t first = r->offset + y * r->columns + x;
  uint32_t bg_color = palette[colors[first] & 0xf];
  hdma2d.LayerCfg[1].InputAlpha = palette[colors[first] >> 4];
  hdma2d.LayerCfg[1].InputOffset = 0;
  // Font stride is constant throughout a region, not recomputed per glyph.
  if (hdma2d.Init.OutputOffset != DISPLAY_WIDTH - r->glyph_width) {
    hdma2d.Init.OutputOffset = DISPLAY_WIDTH - r->glyph_width;
    HAL_DMA2D_Init(&hdma2d);
  }
  HAL_DMA2D_ConfigLayer(&hdma2d, 1);
  const TextFontInfo *asset = text_font(r->definition.font);
  const uint8_t *font = asset->pixels;
  uint16_t glyph_count = asset->count;
  uint16_t *row_dest = framebuffer + r->pixel_offset +
                       y * r->glyph_height * DISPLAY_WIDTH + x * r->glyph_width;
  // Keep the normal text-only loop free of per-cell routing checks. Graphics
  // and overlays are optional destinations, not an always-on compositor.
  const bool covered =
      targets.depth && overlays_intersect((GraphicsRegion){
                           r->definition.x + x * r->glyph_width,
                           r->definition.y + y * r->glyph_height,
                           width * r->glyph_width, height * r->glyph_height});
  if (!covered && !graphics_count) {
    for (uint16_t h = 0; h < height; ++h) {
      uint16_t idx = first + h * r->columns;
      uint16_t *dest = row_dest;
      for (uint16_t w = 0; w < width; ++w) {
        uint8_t char_index = screen[idx++];
        if (char_index >= glyph_count)
          char_index = '?' - 32;
        const uint8_t *glyph = font + char_index * r->glyph_stride;
        HAL_DMA2D_BlendingStart(&hdma2d, (uintptr_t)glyph, bg_color,
                                (uintptr_t)dest, r->glyph_width,
                                r->glyph_height);
        HAL_DMA2D_PollForTransfer(&hdma2d, HAL_MAX_DELAY);
        dest += r->glyph_width;
      }
      row_dest += r->glyph_height * DISPLAY_WIDTH;
    }
    return;
  }
  // Draw each character.
  for (uint16_t h = 0; h < height; ++h) {
    uint16_t idx = first + h * r->columns;
    uint16_t *dest = row_dest;
    for (uint16_t w = 0; w < width; ++w) {
      uint8_t char_index = screen[idx++];
      // Unsupported symbols use the font's '?' glyph.
      if (char_index >= glyph_count)
        char_index = '?' - 32;
      const uint8_t *glyph = font + char_index * r->glyph_stride;
      /* Most of what happens here is defined on the peripheral configuration.
       * Perpheral is configured in memory to memory mode with blending and
       * fixed background color. We take the FG color from the layer config,
       * which we have to update to each region. BG color is defined as a static
       * address with the color in it, so this works as a register to memory
       * DMA2D mode. Char array is contiguous in memory for a single char, but
       * frame buffer needs to have a stride if DISPLAY_WIDTH - CHAR_WIDTH
       * for each line rendered, this is also configured in the peripheral.
       */
      if (!TestBit(graphics_cells, idx - 1)) {
        if (covered) {
          uint32_t offset = dest - framebuffer;
          GraphicsRegion bounds = {offset % DISPLAY_WIDTH,
                                   offset / DISPLAY_WIDTH, r->glyph_width,
                                   r->glyph_height};
          GlyphPaint g = {glyph, bounds, bg_color,
                          hdma2d.LayerCfg[1].InputAlpha};
          route_text(0, bounds, paint_glyph, &g);
          // Restore the region's ordinary strides after clipped fragments.
          configure_glyph(DISPLAY_WIDTH - r->glyph_width, 0, g.fg);
        } else {
          HAL_DMA2D_BlendingStart(&hdma2d, (uintptr_t)glyph, bg_color,
                                  (uintptr_t)dest, r->glyph_width,
                                  r->glyph_height);
          // We could check for return status here, but if it's not returning
          // HAL_OK here we have big problems.
          HAL_DMA2D_PollForTransfer(&hdma2d, HAL_MAX_DELAY);
        }
      }
      dest += r->glyph_width;
    }
    row_dest += r->glyph_height * DISPLAY_WIDTH;
  }
}

// Draw changed detects subregions of the display that changed and that are of
// same parameters (BG and FG colors). Coalescing never crosses a font boundary.
void display_draw_changed(void) {
  if (!dirty_regions)
    return;
  for (uint8_t region = 0; region < layout.count; ++region) {
    if (!(dirty_regions & (1U << region)))
      continue;
    const TextRegionInfo *r = &layout.regions[region];
    uint16_t columns = r->columns;
    uint16_t end = r->offset + columns * r->rows;
    for (uint16_t idx = r->offset; idx < end; ++idx) {
      if (!TestBit(changed, idx))
        continue;
      // Get foreground and background colors of the area.
      uint32_t fg_color = palette[colors[idx] >> 4];
      uint32_t bg_color = palette[colors[idx] & 0xf];
      ClearBit(changed, idx);
      // Check adjacent cells in order to find a bigger rectangle.
      uint16_t local = idx - r->offset;
      uint16_t y = local / columns, x = local % columns;
      uint16_t height = 1;
      // First pass tests the height.
      for (uint16_t probe = idx + columns; probe < end; probe += columns) {
        if (!TestBit(changed, probe) ||
            fg_color != palette[colors[probe] >> 4] ||
            bg_color != palette[colors[probe] & 0xf])
          break;
        ClearBit(changed, probe);
        ++height;
      }
      uint16_t width = 1;
      // Having the height, we can test every subsequent column.
      for (; x + width < columns; ++width) {
        uint16_t h = 0;
        for (; h < height; ++h) {
          uint16_t probe = idx + width + h * columns;
          // If we don't get to max height, then abort.
          if (!TestBit(changed, probe) ||
              fg_color != palette[colors[probe] >> 4] ||
              bg_color != palette[colors[probe] & 0xf])
            break;
          ClearBit(changed, probe);
        }
        if (h != height) {
          // Undo last column.
          while (h) {
            --h;
            SetBit(changed, idx + width + h * columns);
          }
          break;
        }
      }
      draw_cells(r, x, y, width, height);
    }
  }
  dirty_regions = 0;
}

void display_draw_screen(void) {
  display_text_layout();
  // Draw the whole screen through the same dirty-cell path, including colors.
  // Erase old region positions only on layout changes; never per frame.
  if (layout_needs_clear) {
    clear_pixels(COLOR_BG);
  }
  memset(changed, 0xff, (layout.cells + 7) / 8);
  dirty_regions = (1U << layout.count) - 1;
  display_draw_changed();
}

static void paint_solid(void *context, GraphicsRegion r, uint16_t *pixels,
                        uint16_t stride, bool visible) {
  (void)visible;
  uint16_t color = *(uint16_t *)context;
  for (int16_t y = 0; y < r.height; ++y) {
    for (int16_t x = 0; x < r.width; ++x)
      pixels[x] = color;
    pixels += stride;
  }
}

// TODO: update this to use DMA2D ASAP
void display_fill_rect(uint8_t color_index, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height) {
  uint16_t color565 = abgr8888_to_bgr565(palette[color_index]);

  if (width == 0 || height == 0) {
    return;
  }

  // Clip the rectangle to the screen dimensions
  if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }
  if (x + width > DISPLAY_WIDTH) {
    width = DISPLAY_WIDTH - x;
  }
  if (y + height > DISPLAY_HEIGHT) {
    height = DISPLAY_HEIGHT - y;
  }

  if (targets.depth) {
    pixel_route(&targets, active_target, (GraphicsRegion){x, y, width, height},
                paint_solid, &color565);
    return;
  }
  for (uint16_t j = y; j < y + height; j++) {
    for (uint16_t i = x; i < x + width; i++) {
      framebuffer[j * DISPLAY_WIDTH + i] = color565;
    }
  }
}

void display_draw_mono_bitmap(uint16_t x, uint16_t y, const uint8_t *bitmap,
                              uint16_t width, uint16_t height,
                              uint16_t row_bytes, uint8_t fg_color_index,
                              uint8_t bg_color_index) {
  uint16_t fg_color565 = abgr8888_to_bgr565(palette[fg_color_index]);
  uint16_t bg_color565 = abgr8888_to_bgr565(palette[bg_color_index]);

  display_draw_mono_bitmap_rgb565(x, y, bitmap, width, height, row_bytes,
                                  fg_color565, bg_color565);
}

typedef struct {
  const uint8_t *data;
  uint16_t x, y, stride, fg, bg;
} BitmapPaint;
static void paint_bitmap(void *context, GraphicsRegion r, uint16_t *pixels,
                         uint16_t stride, bool visible) {
  (void)visible;
  BitmapPaint *b = context;
  for (int16_t y = 0; y < r.height; ++y) {
    const uint8_t *row = b->data + (r.y + y - b->y) * b->stride;
    for (int16_t x = 0; x < r.width; ++x) {
      uint16_t sx = r.x + x - b->x;
      pixels[x] = (row[sx / 8] & (0x80U >> (sx % 8))) ? b->fg : b->bg;
    }
    pixels += stride;
  }
}

void display_draw_mono_bitmap_rgb565(uint16_t x, uint16_t y,
                                     const uint8_t *bitmap, uint16_t width,
                                     uint16_t height, uint16_t row_bytes,
                                     uint16_t fg_color565,
                                     uint16_t bg_color565) {

  if (!bitmap || width == 0 || height == 0) {
    return;
  }

  if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }
  if (x + width > DISPLAY_WIDTH) {
    width = DISPLAY_WIDTH - x;
  }
  if (y + height > DISPLAY_HEIGHT) {
    height = DISPLAY_HEIGHT - y;
  }

  if (targets.depth) {
    BitmapPaint paint = {bitmap, x, y, row_bytes, fg_color565, bg_color565};
    pixel_route(&targets, active_target, (GraphicsRegion){x, y, width, height},
                paint_bitmap, &paint);
    return;
  }
  for (uint16_t row = 0; row < height; row++) {
    const uint8_t *src = bitmap + row * row_bytes;
    uint16_t *dest = framebuffer + (y + row) * DISPLAY_WIDTH + x;

    for (uint16_t col = 0; col < width; col++) {
      uint8_t byte = src[col >> 3];
      uint8_t mask = (uint8_t)(0x80U >> (col & 7U));
      dest[col] = (byte & mask) ? fg_color565 : bg_color565;
    }
  }
}
