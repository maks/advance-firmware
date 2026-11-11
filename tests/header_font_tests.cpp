/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#include "Adapters/adv/display/display.h"
#include "Adapters/adv/display/font.h"
#include "Application/Views/BaseClasses/UIField.h"
#include "Application/Views/BaseClasses/View.h"
#include "UIFramework/BasicDatas/TextHighlight.h"
#include "UIFramework/BasicDatas/TextLayoutGaps.h"
#include "dma2d.h"
#include "doctest/doctest.h"
#include <cstring>
extern "C" {
uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
uint16_t placeholder_framebuffer_layer2[DISPLAY_WIDTH * DISPLAY_HEIGHT];
DMA2D_HandleTypeDef hdma2d;
uint32_t dmaInitializations, dmaConfigurations;
int HAL_DMA2D_Init(DMA2D_HandleTypeDef *) {
  ++dmaInitializations;
  return 0;
}
int HAL_DMA2D_ConfigLayer(DMA2D_HandleTypeDef *, uint32_t) {
  ++dmaConfigurations;
  return 0;
}
int HAL_DMA2D_PollForTransfer(DMA2D_HandleTypeDef *, uint32_t) { return 0; }
}
namespace {
uint32_t draws, fills, lastWidth, lastHeight;
uint32_t lastFillColor;
uintptr_t lastDest;
const uint8_t *lastGlyph;
// The full-body fixture exercises the original 780-cell rendering workload.
// Map/split-body tests below use the application's default layout directly.
TextLayout FullBodyTestLayout() {
  static constexpr TextRegion regions[] = {{8, 4, 704, 24, TEXT_FONT_SMALL},
                                           {8, 30, 704, 690, TEXT_FONT_NORMAL}};
  return {regions, 2};
}
void Reset() {
  const TextLayout defaults = FullBodyTestLayout();
  REQUIRE(display_configure_text_regions(defaults.regions, defaults.count));
  REQUIRE(display_configure_graphics({nullptr, 0}));
  display_clear(COLOR_BG);
  display_draw_changed();
  draws = fills = 0;
}
} // namespace
extern "C" int HAL_DMA2D_Start(DMA2D_HandleTypeDef *h, uint32_t color,
                               uintptr_t destination, uint32_t width,
                               uint32_t height) {
  ++fills;
  lastFillColor = color;
  CHECK(h->Init.Mode == DMA2D_R2M);
  CHECK(h->Init.OutputOffset == 0);
  CHECK(destination == reinterpret_cast<uintptr_t>(framebuffer));
  CHECK(width == DISPLAY_WIDTH);
  CHECK(height == DISPLAY_HEIGHT);
  uint16_t value = ((color >> 19) & 31) << 11 | ((color >> 10) & 63) << 5 |
                   ((color >> 3) & 31);
  auto *pixels = reinterpret_cast<uint16_t *>(destination);
  for (uint32_t i = 0; i < width * height; ++i)
    pixels[i] = value;
  return 0;
}
extern "C" int HAL_DMA2D_BlendingStart(DMA2D_HandleTypeDef *h, uintptr_t source,
                                       uint32_t background,
                                       uintptr_t destination, uint32_t width,
                                       uint32_t height) {
  ++draws;
  lastWidth = width;
  lastHeight = height;
  lastDest = destination;
  lastGlyph = reinterpret_cast<const uint8_t *>(source);
  auto *out = reinterpret_cast<uint16_t *>(destination);
  uint32_t fg = h->LayerCfg[1].InputAlpha;
  auto rgb565 = [](uint32_t v) -> uint16_t {
    return ((v >> 19) & 31) << 11 | ((v >> 10) & 63) << 5 | ((v >> 3) & 31);
  };
  const uint8_t *in = lastGlyph;
  for (uint32_t y = 0; y < height; ++y) {
    if (destination >= reinterpret_cast<uintptr_t>(framebuffer) &&
        destination < reinterpret_cast<uintptr_t>(
                          framebuffer + DISPLAY_WIDTH * DISPLAY_HEIGHT)) {
      REQUIRE(out >= framebuffer);
      REQUIRE(out + width <= framebuffer + DISPLAY_WIDTH * DISPLAY_HEIGHT);
    }
    for (uint32_t x = 0; x < width; ++x)
      out[x] = rgb565(in[x] ? fg : background);
    out += width + h->Init.OutputOffset;
    in += width + h->LayerCfg[1].InputOffset;
  }
  return 0;
}
TEST_CASE("First row uses native FONTSMALL through its rightmost character") {
  Reset();
  display_putc_region(0, 43, 0, 'A', false);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastWidth == 16);
  CHECK(lastHeight == 24);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 4 * 720 + 696));
  CHECK(memcmp(lastGlyph, &FONTSMALL['A' - 32][0][0], 16 * 24) == 0);
  display_draw_changed();
  CHECK(draws == 1);
  display_putc_region(0, 44, 0, 'B', false);
  display_draw_changed();
  CHECK(draws == 1);
}
TEST_CASE("Adjacent header and body updates retain distinct font strides") {
  Reset();
  display_putc_region(0, 1, 0, 'A', false);
  display_putc_region(1, 1, 0, 'B', false);
  display_draw_changed();
  CHECK(draws == 2);
  CHECK(lastWidth == 22);
  CHECK(lastHeight == 30);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 30 * 720 + 30));
  CHECK(memcmp(lastGlyph, &FONT['B' - 32][0][0], 22 * 30) == 0);
  display_putc_region(0, 2, 0, 'C', false);
  display_draw_changed();
  CHECK(lastWidth == 16);
  CHECK(memcmp(lastGlyph, &FONTSMALL['C' - 32][0][0], 16 * 24) == 0);
}
TEST_CASE("Unsupported header glyphs fall back safely and body bounds reject "
          "overflow") {
  Reset();
  display_putc_region(0, 0, 0, char(255), false);
  display_draw_changed();
  CHECK(memcmp(lastGlyph, &FONTSMALL[0xff - 32][0][0], 16 * 24) == 0);
  display_putc_region(1, 32, 22, 'A', false);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastWidth == 16);
}

TEST_CASE("Regions place fixed fonts at arbitrary pixel origins") {
  const TextRegion regions[] = {{7, 11, 128, 72, TEXT_FONT_SMALL},
                                {139, 17, 88, 150, TEXT_FONT_NORMAL}};
  REQUIRE(display_configure_text_regions(regions, 2));
  display_clear(COLOR_BG);
  draws = 0;
  display_putc_region(0, 2, 1, 'A', false);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 35 * 720 + 39));
  CHECK(lastWidth == 16);
  CHECK(memcmp(lastGlyph, &FONTSMALL['A' - 32][0][0], 384) == 0);
  display_putc_region(1, 3, 4, 'B', true);
  display_draw_changed();
  CHECK(draws == 2);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 137 * 720 + 205));
  CHECK(lastWidth == 22);
  CHECK(memcmp(lastGlyph, &FONT['B' - 32][0][0], 660) == 0);
  display_putc_region(0, 8, 0, 'X', false);
  display_putc_region(0, 0, 3, 'X', false);
  display_putc_region(2, 0, 0, 'X', false);
  display_draw_changed();
  CHECK(draws == 2);
}

TEST_CASE("Region layouts validate geometry transactionally") {
  Reset();
  TextRegionLayout before = *display_text_layout();
  const TextRegion overlap[] = {{0, 0, 160, 48, TEXT_FONT_SMALL},
                                {159, 47, 22, 30, TEXT_FONT_NORMAL}};
  CHECK_FALSE(display_configure_text_regions(overlap, 2));
  CHECK(memcmp(&before, display_text_layout(), sizeof(before)) == 0);
  const TextRegion outside[] = {{710, 0, 16, 24, TEXT_FONT_SMALL}};
  CHECK_FALSE(display_configure_text_regions(outside, 1));
  const TextRegion empty[] = {{0, 0, 0, 24, TEXT_FONT_SMALL}};
  CHECK_FALSE(display_configure_text_regions(empty, 1));
  const TextRegion badFont[] = {{0, 0, 1, 1, 255}};
  CHECK_FALSE(display_configure_text_regions(badFont, 1));
  CHECK_FALSE(display_configure_text_regions(empty, TEXT_REGION_LIMIT + 1));
  // Exact shared boundaries are allowed, with tightly packed storage offsets.
  const TextRegion adjacent[] = {{0, 0, 160, 48, TEXT_FONT_SMALL},
                                 {160, 0, 220, 60, TEXT_FONT_NORMAL}};
  REQUIRE(display_configure_text_regions(adjacent, 2));
  CHECK(display_text_layout()->cells == 40);
  CHECK(display_text_layout()->regions[1].offset == 20);
  const TextRegion dense[] = {{0, 0, 512, 720, TEXT_FONT_SMALL}};
  REQUIRE(display_configure_text_regions(dense, 1));
  CHECK(display_text_layout()->cells == 960);
  Reset();
}

TEST_CASE("Local text submission clips without touching neighboring regions") {
  TextRegionLayout layout{};
  const TextRegion regions[] = {{0, 0, 64, 48, TEXT_FONT_SMALL},
                                {100, 0, 66, 30, TEXT_FONT_NORMAL}};
  REQUIRE(text_region_plan(&layout, regions, 2));
  uint8_t chars[12], properties[12];
  memset(chars, '.', sizeof(chars));
  memset(properties, 0, sizeof(properties));
  CHECK(text_region_write(&layout.regions[0], chars, properties, -2, 1,
                          "abcdefghi", 0x83) == 0);
  CHECK(text_region_write(&layout.regions[0], chars, properties, 0, 1,
                          "cdefghi", 0x83) == 4);
  CHECK(memcmp(chars, "....cdef....", 12) == 0);
  CHECK(properties[3] == 0);
  CHECK(properties[4] == 0x83);
  CHECK(properties[7] == 0x83);
  CHECK(properties[8] == 0);
  CHECK(text_region_write(&layout.regions[1], chars, properties, 2, 0, "XYZ",
                          1) == 1);
  CHECK(chars[10] == 'X');
  CHECK(chars[11] == '.');
  CHECK(text_region_write(&layout.regions[0], chars, properties, -100, 0,
                          "short", 1) == 0);
  CHECK(text_region_write(&layout.regions[0], chars, properties, 0, 2,
                          "outside", 1) == 0);
  CHECK(text_region_write(&layout.regions[0], chars, properties, 0, -1,
                          "outside", 1) == 0);
}

TEST_CASE(
    "Dirty coalescing renders every changed cell once across mixed regions") {
  Reset();
  display_set_foreground(COLOR_NORMAL);
  for (uint8_t y = 0; y < 23; ++y) {
    for (uint8_t x = 0; x < 32; ++x) {
      display_putc_region(TEXT_REGION_BODY, x, y, 'A', (x + y) % 3 == 0);
    }
  }
  for (uint8_t x = 0; x < 44; ++x)
    display_putc_region(TEXT_REGION_HEADER, x, 0, 'B', false);
  display_draw_changed();
  CHECK(draws == 780);
  display_draw_changed();
  CHECK(draws == 780);
  draws = 0;
  display_clear(COLOR_BG);
  CHECK(draws == 0);
  CHECK(fills == 1);
  display_draw_changed();
  CHECK(draws == 0);
}

TEST_CASE(
    "Reapplying a layout preserves pending changes and switching clears them") {
  Reset();
  display_putc_region(0, 1, 0, 'A', false);
  const TextLayout defaults = FullBodyTestLayout();
  REQUIRE(display_configure_text_regions(defaults.regions, defaults.count));
  display_draw_changed();
  CHECK(draws == 1);
  const TextRegion custom[] = {{0, 60, 44, 30, TEXT_FONT_NORMAL}};
  display_putc_region(0, 2, 0, 'B', false);
  REQUIRE(display_configure_text_regions(custom, 1));
  display_clear(COLOR_BG);
  draws = 0;
  display_draw_changed();
  CHECK(draws == 0);
  Reset();
}

TEST_CASE("Whole-screen clears use one DMA fill and restore glyph mode") {
  Reset();
  hdma2d.Init.Mode = 5; // Mock blending mode, distinct from R2M.
  hdma2d.Init.OutputOffset = 698;
  display_set_palette_color(COLOR_BG, 0x1234);
  display_clear(COLOR_BG);
  CHECK(fills == 1);
  CHECK(draws == 0);
  CHECK(lastFillColor == rgb565_to_abgr8888(0x1234));
  CHECK(hdma2d.Init.Mode == 5);
  CHECK(hdma2d.Init.OutputOffset == 698);
  display_draw_changed();
  CHECK(draws == 0);
  display_set_foreground(COLOR_NORMAL);
  display_putc_region(0, 43, 0, 'A', true);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(hdma2d.Init.Mode == 5);
  CHECK(hdma2d.Init.OutputOffset == 704);
  CHECK(hdma2d.LayerCfg[1].InputAlpha == rgb565_to_abgr8888(0x1234));
  display_set_palette_color(COLOR_BG, 0);
}

TEST_CASE("Missing UI layout is rejected without discarding pending text") {
  Reset();
  const TextRegionLayout before = *display_text_layout();
  display_putc_region(TEXT_REGION_HEADER, 2, 0, 'A', false);
  CHECK_FALSE(display_configure_text_regions(nullptr, 0));
  CHECK_FALSE(display_configure_text_regions(nullptr, 2));
  CHECK(memcmp(&before, display_text_layout(), sizeof(before)) == 0);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 4 * 720 + 40));
  CHECK(memcmp(lastGlyph, &FONTSMALL['A' - 32][0][0], 384) == 0);
}

TEST_CASE(
    "A selected layout is copied and the base view layout can be restored") {
  Reset();
  TextRegion custom[] = {{123, 45, 88, 60, TEXT_FONT_NORMAL}};
  REQUIRE(display_configure_text_regions(custom, 1));
  display_clear(COLOR_BG);
  // Changing the caller's storage must not change the installed layout.
  custom[0].x = 0;
  custom[0].width = 22;
  display_putc_region(0, 3, 1, 'B', false);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 75 * 720 + 189));
  Reset();
  display_putc_region(TEXT_REGION_BODY, 3, 1, 'C', false);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 60 * 720 + 74));
}

TEST_CASE("Background gap fills preserve glyphs graphs and live overlays") {
  Reset();
  display_set_palette_color(COLOR_BG, 0);
  display_clear(COLOR_BG);
  const GraphicsRegion graph[] = {{40, 40, 22, 30}};
  REQUIRE(display_configure_graphics({graph, 1}));
  display_draw_changed();
  display_fill_rect(COLOR_WHITE, 40, 40, 22, 30);
  REQUIRE(display_push_overlay({0, 0, 80, 80}));
  display_select_target(1);
  display_fill_rect(COLOR_BG, 0, 0, 80, 80);
  display_select_target(0);
  const uint32_t revision = display_graphics_revision();

  display_set_palette_color(COLOR_BG, 0xffff);
  fills = draws = 0;
  PaintTextLayoutGaps(*display_text_layout(), display_graphics_layout(),
                      DISPLAY_WIDTH, DISPLAY_HEIGHT, [](GraphicsRegion r) {
                        display_fill_rect(COLOR_BG, r.x, r.y, r.width,
                                          r.height);
                      });
  CHECK(fills == 0); // No full-screen DMA clear or glyph transfers.
  CHECK(draws == 0);
  CHECK(display_overlay_depth() == 1);
  CHECK(display_graphics_revision() == revision);
  CHECK(framebuffer[28 * 720 + 8] == 0); // Existing popup remains visible.
  CHECK(framebuffer[28 * 720 + 100] == 0xffff); // Uncovered gap updates now.
  CHECK(framebuffer[4 * 720 + 100] == 0);       // Text is not erased.
  display_pop_overlay();
  CHECK(framebuffer[28 * 720 + 8] == 0xffff); // Backing has the updated gap.
  CHECK(framebuffer[0] == 0xffff);
  CHECK(framebuffer[4 * 720 + 8] == 0);        // Cached text remains untouched.
  CHECK(framebuffer[40 * 720 + 40] == 0xffff); // Graph remains untouched.
  CHECK(display_graphics_revision() == revision);
  display_set_palette_color(COLOR_BG, 0);
  Reset();
}

TEST_CASE("A graph rejects intersecting glyphs whole") {
  Reset();
  const GraphicsRegion region = {7, 7, 37, 24};
  REQUIRE(display_configure_graphics({&region, 1}));
  display_draw_changed();
  display_fill_rect(COLOR_WHITE, 7, 7, 37, 24);
  draws = 0;
  display_putc_region(0, 0, 0, 'A', false);
  display_putc_region(0, 1, 0, 'B', false);
  display_draw_changed();
  CHECK(draws == 0);
  for (int y = 7; y < 31; ++y)
    for (int x = 7; x < 44; ++x)
      CHECK(framebuffer[y * 720 + x] == 0xffff);
  GraphicsRegion bad[] = {region, {20, 20, 10, 10}};
  CHECK_FALSE(display_configure_graphics({bad, 2}));
  CHECK(display_graphics_layout().count == 1);
  REQUIRE(display_configure_graphics({nullptr, 0}));
  display_draw_changed();
  CHECK(framebuffer[30 * 720 + 7] == 0);
  Reset();
}

TEST_CASE(
    "Clipped native glyphs update live backing and restore without a redraw") {
  static uint16_t expected[DISPLAY_WIDTH * DISPLAY_HEIGHT];
  Reset();
  display_set_foreground(COLOR_NORMAL);
  display_set_background(COLOR_BG);
  display_putc_region(0, 0, 0, 'B', false);
  display_draw_changed();
  memcpy(expected, framebuffer, sizeof(expected));
  Reset();
  display_putc_region(0, 0, 0, 'A', false);
  display_draw_changed();
  REQUIRE(display_push_overlay({7, 7, 37, 24}));
  display_select_target(1);
  display_fill_rect(COLOR_WHITE, 7, 7, 37, 24);
  display_select_target(0);
  display_putc_region(0, 0, 0, 'B', false);
  display_draw_changed();
  for (int y = 0; y < 720; ++y)
    for (int x = 0; x < 720; ++x) {
      if (x >= 7 && x < 44 && y >= 7 && y < 31)
        CHECK(framebuffer[y * 720 + x] == 0xffff);
      else
        CHECK(framebuffer[y * 720 + x] == expected[y * 720 + x]);
    }
  display_pop_overlay();
  CHECK(memcmp(expected, framebuffer, sizeof(expected)) == 0);
  uint32_t before = draws;
  display_draw_changed();
  CHECK(draws == before);
  Reset();
}

TEST_CASE("Live pixel routing matches independently retained layers") {
  constexpr int W = 32, H = 24, N = W * H;
  static uint16_t front[N], storage[2][N], layers[3][N];
  static uint8_t writes[N];
  memset(front, 0, sizeof(front));
  memset(layers, 0, sizeof(layers));
  PixelTargets targets = {front, W, H, 0, {}};
  GraphicsRegion bounds[2]{};
  uint8_t depth = 0;
  uint32_t rng = 341;
  auto next = [&]() {
    rng = rng * 1664525U + 1013904223U;
    return rng;
  };
  struct Fill {
    uint16_t color;
    uint8_t *writes;
  };
  auto paint = [](void *ctx, GraphicsRegion r, uint16_t *dest, uint16_t stride,
                  bool) {
    auto &f = *static_cast<Fill *>(ctx);
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        CHECK(++f.writes[(r.y + y) * W + r.x + x] == 1);
        dest[y * stride + x] = f.color;
      }
  };
  auto inside = [](GraphicsRegion r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
  };
  for (int operation = 0; operation < 1500; ++operation) {
    uint32_t choice = next() % 10;
    if (choice == 0 && depth < 2) {
      int x = next() % W, y = next() % H;
      GraphicsRegion r = {int16_t(x), int16_t(y), int16_t(1 + next() % (W - x)),
                          int16_t(1 + next() % (H - y))};
      CHECK_FALSE(pixel_push(&targets, r, storage[depth], 0));
      REQUIRE(pixel_push(&targets, r, storage[depth], N));
      memcpy(layers[depth + 1], front, sizeof(front));
      bounds[depth++] = r;
    } else if (choice == 1 && depth) {
      REQUIRE(pixel_pop(&targets));
      --depth;
    } else {
      uint8_t target = next() % (depth + 1);
      GraphicsRegion r = {int16_t(int(next() % W) - 2),
                          int16_t(int(next() % H) - 2), int16_t(1 + next() % W),
                          int16_t(1 + next() % H)};
      Fill fill = {uint16_t(next()), writes};
      memset(writes, 0, sizeof(writes));
      pixel_route(&targets, target, r, paint, &fill);
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          bool affected =
              inside(r, x, y) && (!target || inside(bounds[target - 1], x, y));
          CHECK(writes[y * W + x] == (affected ? 1 : 0));
          if (affected)
            layers[target][y * W + x] = fill.color;
        }
    }
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        uint8_t visible = 0;
        for (uint8_t i = 0; i < depth; ++i)
          if (inside(bounds[i], x, y))
            visible = i + 1;
        REQUIRE(front[y * W + x] == layers[visible][y * W + x]);
      }
  }
}

TEST_CASE(
    "Framebuffer invalidation preserves analysis caches across overlays") {
  Reset();
  const uint32_t initial = display_graphics_revision();
  REQUIRE(display_push_overlay({0, 0, 44, 60}));
  display_select_target(1);
  display_draw_overlay_char(0, 0, 'A', false);
  display_select_target(0);
  const uint8_t bitmap[] = {0xa0, 0x40};
  display_draw_mono_bitmap_rgb565(0, 0, bitmap, 3, 2, 1, 0xffff, 0x1234);
  display_pop_overlay();
  CHECK(display_graphics_revision() == initial);
  CHECK(framebuffer[0] == 0xffff);
  CHECK(framebuffer[1] == 0x1234);
  CHECK(framebuffer[2] == 0xffff);
  CHECK(framebuffer[DISPLAY_WIDTH] == 0x1234);
  CHECK(framebuffer[DISPLAY_WIDTH + 1] == 0xffff);
  CHECK(framebuffer[DISPLAY_WIDTH + 2] == 0x1234);
  const GraphicsRegion graph[] = {{10, 60, 100, 100}};
  REQUIRE(display_configure_graphics({graph, 1}));
  CHECK(display_graphics_revision() != initial);
  const uint32_t configured = display_graphics_revision();
  REQUIRE(display_configure_graphics({graph, 1}));
  CHECK(display_graphics_revision() == configured);
  display_clear(COLOR_BG);
  CHECK(display_graphics_revision() != configured);
  Reset();
}

TEST_CASE("Uncovered header retains batched DMA setup while a popup is open") {
  Reset();
  REQUIRE(display_push_overlay({8, 300, 704, 90}));
  display_select_target(0);
  dmaInitializations = dmaConfigurations = 0;
  display_set_foreground(COLOR_NORMAL);
  for (uint8_t x = 0; x < 44; ++x)
    display_putc_region(0, x, 0, 'A', false);
  display_draw_changed();
  CHECK(draws == 44);
  CHECK(dmaInitializations <= 1);
  CHECK(dmaConfigurations == 1);
  display_pop_overlay();
  Reset();
}

TEST_CASE(
    "Overlay glyphs reuse DMA setup and reconfigure after a clipped glyph") {
  Reset();
  display_set_foreground(COLOR_NORMAL);
  REQUIRE(display_push_overlay({15, 300, 697, 90}));
  display_select_target(1);
  // First glyph crosses the left boundary and programs a source offset.
  display_draw_overlay_char(0, 10, 'A', false);
  CHECK(hdma2d.LayerCfg[1].InputOffset == 7);
  display_draw_overlay_char(1, 10, 'B', false);
  CHECK(hdma2d.LayerCfg[1].InputOffset == 0);
  CHECK(hdma2d.Init.OutputOffset == DISPLAY_WIDTH - CHAR_WIDTH);
  dmaInitializations = dmaConfigurations = 0;
  for (uint8_t x = 2; x < 32; ++x)
    display_draw_overlay_char(x, 10, 'C', false);
  CHECK(dmaInitializations == 0);
  CHECK(dmaConfigurations == 0);
  // Color changes still reach the hardware.
  display_draw_overlay_char(2, 10, 'C', true);
  CHECK(dmaConfigurations == 1);
  display_select_target(0);
  display_pop_overlay();
  Reset();
}

TEST_CASE("Region IDs have no meaning beyond their layout index") {
  TextRegionLayout planned{};
  const TextRegion regions[] = {{300, 300, 48, 48, TEXT_FONT_SMALL},
                                {20, 90, 64, 24, TEXT_FONT_SMALL},
                                {200, 40, 44, 90, TEXT_FONT_NORMAL}};
  REQUIRE(text_region_plan(&planned, regions, 3));
  uint8_t chars[16], props[16]{};
  memset(chars, '.', sizeof(chars));
  CHECK(text_region_write(&planned.regions[1], chars, props, 1, 0, "ABCD", 7) ==
        3);
  CHECK(memcmp(chars, ".......ABC......", 16) == 0);
  CHECK(text_region_write(&planned.regions[0], chars, props, -1, 0, "X", 1) ==
        0);
  REQUIRE(display_configure_text_regions(regions, 3));
  display_clear(COLOR_BG);
  draws = 0;
  display_putc_region(0, 2, 1, 'M', false);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 324 * 720 + 332));
  CHECK(lastWidth == 16);
  CHECK(lastHeight == 24);
  CHECK(memcmp(lastGlyph, &FONTSMALL['M' - 32][0][0], 16 * 24) == 0);
  Reset();
}

TEST_CASE("Map glyphs use native font stride and survive overlay restoration") {
  Reset();
  const TextLayout definition = View::GetDefaultTextLayout();
  REQUIRE(display_configure_text_regions(definition.regions, definition.count));
  display_clear(COLOR_BG);
  display_set_foreground(COLOR_NORMAL);
  display_putc_region(TEXT_REGION_MAP, 4, 4, 'X', false);
  display_draw_changed();
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 696 * 720 + 72));
  CHECK(lastWidth == 16);
  CHECK(lastHeight == 24);
  // Separate translation units have separate static font arrays: compare data.
  CHECK(memcmp(lastGlyph, &FONTSMALL['X' - 32][0][0], sizeof(FONTSMALL[0])) ==
        0);
  static uint16_t expected[80 * 120];
  for (int y = 0; y < 120; ++y)
    memcpy(expected + y * 80, framebuffer + (600 + y) * 720 + 8,
           80 * sizeof(uint16_t));
  REQUIRE(display_push_overlay({8, 600, 80, 120}));
  display_select_target(1);
  display_fill_rect(COLOR_WHITE, 8, 600, 80, 120);
  display_select_target(0);
  display_putc_region(TEXT_REGION_MAP, 4, 4, 'X', false);
  display_draw_changed();
  display_pop_overlay();
  for (int y = 0; y < 120; ++y)
    CHECK(memcmp(expected + y * 80, framebuffer + (600 + y) * 720 + 8,
                 80 * sizeof(uint16_t)) == 0);
  display_putc_region(TEXT_REGION_MAP, 0, 0, char(0xff), false);
  display_draw_changed();
  CHECK(memcmp(lastGlyph, &FONTSMALL[0xff - 32][0][0], sizeof(FONTSMALL[0])) ==
        0);
  Reset();
}

TEST_CASE(
    "Remote overlay notifications carry lifecycle only, never glyph pixels") {
  Reset();
  static uint32_t calls;
  static DisplayOverlayEvent events[4];
  calls = 0;
  display_set_overlay_observer([](DisplayOverlayEvent event, GraphicsRegion) {
    REQUIRE(calls < 4);
    events[calls++] = event;
  });
  REQUIRE(display_push_overlay({7, 7, 37, 24}));
  CHECK(calls == 1);
  CHECK(events[0] == DISPLAY_OVERLAY_PUSH);
  display_putc_region(0, 0, 0, 'A', false);
  display_draw_changed(); // Partly obscured text must not trigger readback.
  display_select_target(1);
  display_draw_overlay_char(0, 0, 'B', false);
  CHECK(calls == 1);
  display_pop_overlay();
  CHECK(calls == 2);
  CHECK(events[1] == DISPLAY_OVERLAY_POP);
  REQUIRE(display_push_overlay({8, 600, 80, 120}));
  display_discard_overlays();
  CHECK(calls == 4);
  CHECK(events[3] == DISPLAY_OVERLAY_RESET);
  display_set_overlay_observer(nullptr);
  Reset();
}

TEST_CASE("Pixel regions allocate only complete native glyph cells") {
  Reset();
  const TextRegion definition[] = {{7, 11, 33, 49, TEXT_FONT_SMALL}};
  REQUIRE(display_configure_text_regions(definition, 1));
  const TextRegionInfo &region = display_text_layout()->regions[0];
  CHECK(region.columns == 2);
  CHECK(region.rows == 2);
  CHECK(display_text_layout()->cells == 4);
  display_clear(COLOR_BG);
  draws = 0;
  display_putc_region(0, 1, 1, 'A', false);
  display_putc_region(0, 2, 0, 'B', false);
  display_putc_region(0, 0, 2, 'C', false);
  display_putc_region(1, 0, 0, 'D', false);
  display_draw_changed();
  CHECK(draws == 1);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 35 * 720 + 23));
  for (int y = 11; y < 60; ++y)
    CHECK(framebuffer[y * 720 + 39] == 0);
  for (int x = 7; x < 40; ++x)
    CHECK(framebuffer[59 * 720 + x] == 0);
  Reset();
}

TEST_CASE(
    "Unbuildable pixel regions leave the active layout and dirty text intact") {
  Reset();
  TextRegionLayout before = *display_text_layout();
  display_putc_region(0, 0, 0, 'A', false);
  const TextRegion tooSmall[] = {{0, 0, 14, 24, TEXT_FONT_SMALL}};
  const TextRegion exceedsPool[] = {{0, 0, 720, 720, TEXT_FONT_SMALL}};
  const TextRegion overflow[] = {{65000, 0, 1000, 30, TEXT_FONT_NORMAL}};
  CHECK_FALSE(display_configure_text_regions(tooSmall, 1));
  CHECK_FALSE(display_configure_text_regions(exceedsPool, 1));
  CHECK_FALSE(display_configure_text_regions(overflow, 1));
  CHECK(memcmp(&before, display_text_layout(), sizeof(before)) == 0);
  display_draw_changed();
  CHECK(draws == 1);
  Reset();
}

TEST_CASE(
    "Field region binding translates drawing without changing navigation") {
  class Field : public UIField {
  public:
    Field() : UIField(GUIPoint(12, 21)) {}
    void Draw(GUIWindow &, int) override {}
    void OnClick() override {}
    void ProcessArrow(unsigned short) override {}
  } field;
  field.SetTextRegion(6, 4, 19);
  CHECK(field.GetTextRegion() == 6);
  CHECK(field.GetTextPosition() == GUIPoint(8, 2));
  CHECK(field.GetPosition() == GUIPoint(12, 21));
  GUIPoint moved(15, 22);
  field.SetPosition(moved);
  CHECK(field.GetTextPosition() == GUIPoint(11, 3));
  field.SetTextRegion(3);
  CHECK(field.GetTextPosition() == moved);
}

TEST_CASE(
    "Stacked small-font rows and normal rows retain independent dirty groups") {
  Reset();
  const TextRegion regions[] = {{8, 36, 64, 48, TEXT_FONT_SMALL},
                                {8, 90, 88, 60, TEXT_FONT_NORMAL}};
  REQUIRE(display_configure_text_regions(regions, 2));
  display_clear(COLOR_BG);
  draws = dmaConfigurations = 0;
  for (uint8_t region = 0; region < 2; ++region)
    for (uint8_t row = 0; row < 2; ++row)
      for (uint8_t column = 0; column < 4; ++column)
        display_putc_region(region, column, row, 'A', false);
  display_draw_changed();
  CHECK(draws == 16);
  CHECK(dmaConfigurations == 2);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 120 * 720 + 74));
  // The gap between the two font grids is never touched by their transfers.
  for (int y = 84; y < 90; ++y)
    for (int x = 8; x < 96; ++x)
      CHECK(framebuffer[y * 720 + x] == 0);
  display_draw_changed();
  CHECK(draws == 16);
  display_putc_region(0, 3, 1, 'B', true);
  display_draw_changed();
  CHECK(draws == 17);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(framebuffer + 60 * 720 + 56));
  CHECK(lastWidth == 16);
  CHECK(lastHeight == 24);
  CHECK(memcmp(lastGlyph, &FONTSMALL['B' - 32][0][0], sizeof(FONTSMALL[0])) ==
        0);
  Reset();
}

TEST_CASE("Graphics ownership spans bitmap bytes and respects region offsets") {
  Reset();
  const TextRegion regions[] = {{8, 0, 66, 30, TEXT_FONT_NORMAL},
                                {8, 30, 220, 30, TEXT_FONT_NORMAL}};
  REQUIRE(display_configure_text_regions(regions, 2));
  display_clear(COLOR_BG);
  display_set_palette_color(COLOR_BG, 0);
  display_set_palette_color(COLOR_NORMAL, 0xffff);
  display_set_background(COLOR_BG);
  display_set_foreground(COLOR_NORMAL);
  // The second region starts at cell 3, so local cells 4/5 are bits 7/8.
  GraphicsRegion graph[] = {{96, 30, 44, 30}};
  REQUIRE(display_configure_graphics({graph, 1}));
  for (uint8_t r = 0; r < 2; ++r)
    for (uint8_t x = 0; x < (r ? 10 : 3); ++x)
      display_putc_region(r, x, 0, ' ', true);
  display_draw_changed();
  auto checkCells = [](int firstCovered, int lastCovered) {
    for (int x = 0; x < 3; ++x)
      CHECK(framebuffer[15 * 720 + 8 + x * 22 + 11] == 0xffff);
    for (int x = 0; x < 10; ++x) {
      const uint16_t expected =
          x >= firstCovered && x <= lastCovered ? 0 : 0xffff;
      CHECK(framebuffer[45 * 720 + 8 + x * 22 + 11] == expected);
    }
  };
  checkCells(4, 5);
  // Moving ownership must clear old bits while retaining neighbouring text.
  graph[0].x += 22;
  REQUIRE(display_configure_graphics({graph, 1}));
  display_fill_rect(COLOR_BG, graph[0].x, graph[0].y, graph[0].width,
                    graph[0].height);
  display_draw_changed();
  checkCells(5, 6);
  REQUIRE(display_configure_graphics({nullptr, 0}));
  display_draw_changed();
  checkCells(-1, -1);
  Reset();
}

TEST_CASE("Rounded highlight corner updates survive live overlay restoration") {
  Reset();
  display_set_palette_color(COLOR_BG, 0);
  display_set_palette_color(COLOR_NORMAL, 0xffff);
  display_set_background(COLOR_BG);
  display_set_foreground(COLOR_NORMAL);
  auto paint = [](uint8_t corners) {
    TextHighlightPaint(corners, 8, 30, 22, 30, [](int x, int y, int w, int h) {
      display_fill_rect(COLOR_BG, x, y, w, h);
    });
  };
  display_putc_region(1, 0, 0, ' ', true);
  display_draw_changed();
  paint(15);
  CHECK(framebuffer[30 * 720 + 8] == 0);
  CHECK(framebuffer[30 * 720 + 10] == 0xffff);
  REQUIRE(display_push_overlay({8, 30, 44, 30}));
  display_select_target(1);
  display_fill_rect(COLOR_BG, 8, 30, 44, 30);
  display_select_target(0);
  // Grow the hidden selection rightward. The old right-hand corners must be
  // filled back in, while the left corners remain rounded in live backing.
  display_putc_region(1, 0, 0, ' ', true);
  display_putc_region(1, 1, 0, ' ', true);
  display_draw_changed();
  paint(5);
  CHECK(framebuffer[30 * 720 + 29] == 0);
  display_pop_overlay();
  CHECK(framebuffer[30 * 720 + 8] == 0);
  CHECK(framebuffer[30 * 720 + 29] == 0xffff);
  CHECK(framebuffer[59 * 720 + 29] == 0xffff);
}

TEST_CASE(
    "Graphics ownership invalidation restores unchanged highlight corners") {
  Reset();
  const TextRegion region[] = {{8, 30, 66, 30, TEXT_FONT_NORMAL}};
  REQUIRE(display_configure_text_regions(region, 1));
  display_clear(COLOR_BG);
  display_set_palette_color(COLOR_BG, 0);
  display_set_palette_color(COLOR_NORMAL, 0xffff);
  display_set_background(COLOR_BG);
  display_set_foreground(COLOR_NORMAL);
  const uint8_t chars[] = {' ', ' ', ' '};
  const uint8_t props[] = {0x81, 0x81, 0};
  uint8_t previous[3] = {}, previousProps[3] = {};
  // Model the application cache alongside the real backend. Both caches must
  // invalidate on ownership changes, even though glyphs/properties are equal.
  auto configure = [&](GraphicsLayout graphics) {
    const uint32_t revision = display_graphics_revision();
    const bool ok = display_configure_graphics(graphics);
    if (display_graphics_revision() != revision) {
      memset(previous, 0xff, sizeof(previous));
      memset(previousProps, 0xff, sizeof(previousProps));
    }
    return ok;
  };
  auto flush = [&] {
    uint8_t dirty[1];
    TextHighlightChanges(chars, props, previous, previousProps, 3, 1, dirty);
    for (uint8_t x = 0; x < 3; ++x) {
      if (!(dirty[0] & (1U << x)))
        continue;
      display_putc_region(0, x, 0, chars[x], props[x] & 0x80);
      previous[x] = chars[x];
      previousProps[x] = props[x];
    }
    display_draw_changed();
    for (uint8_t x = 0; x < 3; ++x) {
      if (!(dirty[0] & (1U << x)))
        continue;
      const GraphicsRegion cell = {int16_t(8 + x * 22), 30, 22, 30};
      bool covered = false;
      const GraphicsLayout graphics = display_graphics_layout();
      for (uint8_t i = 0; i < graphics.count; ++i) {
        const GraphicsRegion hit =
            pixel_intersection(cell, graphics.regions[i]);
        covered |= hit.width && hit.height;
      }
      if (!covered)
        TextHighlightPaint(TextHighlightCorners(props, 3, 1, x, 0), cell.x,
                           cell.y, cell.width, cell.height,
                           [](int px, int py, int w, int h) {
                             display_fill_rect(COLOR_BG, px, py, w, h);
                           });
    }
  };
  flush();
  static uint16_t expected[66 * 30];
  for (int y = 0; y < 30; ++y)
    memcpy(expected + y * 66, framebuffer + (30 + y) * 720 + 8,
           66 * sizeof(uint16_t));
  auto checkAppearance = [&] {
    for (int y = 0; y < 30; ++y)
      CHECK(memcmp(expected + y * 66, framebuffer + (30 + y) * 720 + 8,
                   66 * sizeof(uint16_t)) == 0);
  };
  // Even a graph outside the highlight makes the backend repaint its glyphs.
  GraphicsRegion graph[] = {{52, 30, 22, 30}};
  REQUIRE(configure({graph, 1}));
  flush();
  checkAppearance();
  draws = fills = 0;
  REQUIRE(configure({graph, 1}));
  flush();
  CHECK(draws == 0);
  CHECK(fills == 0);
  const GraphicsRegion invalid[] = {{719, 30, 22, 30}};
  CHECK_FALSE(configure({invalid, 1}));
  flush();
  CHECK(draws == 0);
  CHECK(fills == 0);
  // Cover the highlight, then move away and finally remove the graph.
  graph[0] = {8, 30, 44, 30};
  REQUIRE(configure({graph, 1}));
  display_fill_rect(COLOR_NORMAL, 8, 30, 44, 30);
  flush();
  CHECK(framebuffer[30 * 720 + 8] == 0xffff);
  graph[0] = {52, 30, 22, 30};
  REQUIRE(configure({graph, 1}));
  flush();
  checkAppearance();
  REQUIRE(configure({nullptr, 0}));
  flush();
  checkAppearance();
  Reset();
}

TEST_CASE("Overlay line glyphs preserve extended character bytes through DMA") {
  Reset();
  display_set_background(COLOR_BG);
  display_set_foreground(COLOR_NORMAL);
  REQUIRE(display_push_overlay({8, 30, 6 * 22, 30}));
  display_select_target(1);
  const uint8_t glyphs[] = {196, 179, 218, 191, 192, 217};
  for (uint8_t x = 0; x < sizeof(glyphs); ++x) {
    display_draw_overlay_char(x, 1, static_cast<char>(glyphs[x]), false);
    CHECK(lastWidth == CHAR_WIDTH);
    CHECK(lastHeight == CHAR_HEIGHT);
    CHECK(lastDest ==
          reinterpret_cast<uintptr_t>(framebuffer + 30 * 720 + 8 + x * 22));
    CHECK(memcmp(lastGlyph, &FONT[glyphs[x] - 32][0][0],
                 CHAR_WIDTH * CHAR_HEIGHT) == 0);
  }
  display_pop_overlay();
}

TEST_CASE("Small extended glyphs use their native slots through DMA") {
  Reset();
  const uint8_t codes[] = {128, 179, 191, 192, 196, 217, 218, 255};
  for (uint8_t code : codes) {
    display_putc_region(0, 0, 0, static_cast<char>(code), false);
    display_draw_changed();
    CHECK(lastWidth == 16);
    CHECK(lastHeight == 24);
    CHECK(memcmp(lastGlyph, &FONTSMALL[code - 32][0][0],
                 sizeof(FONTSMALL[0])) == 0);
  }
}

TEST_CASE("Tiny stereo meter renders independently of adjacent body text") {
  Reset();
  const TextLayout definition = View::GetDefaultTextLayout();
  REQUIRE(display_configure_text_regions(definition.regions, definition.count));
  display_clear(COLOR_BG);
  display_set_foreground(COLOR_NORMAL);
  const TextRegionInfo &vu = display_text_layout()->regions[TEXT_REGION_VU];
  const TextRegionInfo &body = display_text_layout()->regions[TEXT_REGION_BODY];
  // A body glyph and two different meter columns exercise coalescing across
  // different font strides and the rightmost physical framebuffer boundary.
  display_putc_region(TEXT_REGION_BODY, body.columns - 1, 0, 'A', false);
  for (uint8_t y = 0; y < vu.rows; ++y) {
    display_putc_region(TEXT_REGION_VU, 0, y, char(0xdb), false);
    display_putc_region(TEXT_REGION_VU, 1, y, ' ', false);
  }
  display_draw_changed();
  CHECK(lastWidth == 10);
  CHECK(lastHeight == 15);
  CHECK(memcmp(lastGlyph, &FONTTINY[0][0][0], sizeof(FONTTINY[0])) == 0);
  const uint32_t before = draws;
  display_draw_changed();
  CHECK(draws == before);
  display_putc_region(TEXT_REGION_VU, 1, vu.rows - 1, char(0xdb), false);
  display_draw_changed();
  CHECK(draws == before + 1);
  CHECK(lastDest == reinterpret_cast<uintptr_t>(
                        framebuffer +
                        (vu.definition.y + (vu.rows - 1) * vu.glyph_height) *
                            DISPLAY_WIDTH +
                        vu.definition.x + vu.glyph_width));
  CHECK(memcmp(lastGlyph, &FONTTINY[0xdb - 32][0][0], sizeof(FONTTINY[0])) ==
        0);
  Reset();
}
