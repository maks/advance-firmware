/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Unit tests for the extracted shared PicoTracker display renderer.
 */

#include "doctest/doctest.h"

#include "System/Display/Display.h"
#include "System/Display/DisplayBackend.h"
#include "System/Display/font.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
uint16_t testFramebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
bool testDirty = false;

struct RegionCall {
  uint8_t x;
  uint8_t y;
  uint8_t width;
  uint8_t height;
  uint32_t bg;
  uint32_t fg;
};
std::vector<RegionCall> recordedRegions;

void ResetTestDisplay() {
  std::memset(testFramebuffer, 0, sizeof(testFramebuffer));
  testDirty = false;
  recordedRegions.clear();
}
} // namespace

extern "C" {
uint16_t *display_backend_get_framebuffer(void) { return testFramebuffer; }

void display_backend_mark_dirty(void) { testDirty = true; }

void display_backend_draw_region(uint8_t x, uint8_t y, uint8_t width,
                                 uint8_t height, uint32_t bg_color,
                                 uint32_t fg_color,
                                 const uint8_t *screen_chars) {
  recordedRegions.push_back({x, y, width, height, bg_color, fg_color});

  // Also simulate the WASM software A8 blend to test clipping and pixel safety
  const uint32_t fg_r = fg_color & 0xFFU;
  const uint32_t fg_g = (fg_color >> 8U) & 0xFFU;
  const uint32_t fg_b = (fg_color >> 16U) & 0xFFU;

  const uint32_t bg_r = bg_color & 0xFFU;
  const uint32_t bg_g = (bg_color >> 8U) & 0xFFU;
  const uint32_t bg_b = (bg_color >> 16U) & 0xFFU;

  const uint16_t fg565 = static_cast<uint16_t>(
      ((fg_b >> 3U) << 11U) | ((fg_g >> 2U) << 5U) | (fg_r >> 3U));
  const uint16_t bg565 = static_cast<uint16_t>(
      ((bg_b >> 3U) << 11U) | ((bg_g >> 2U) << 5U) | (bg_r >> 3U));

  for (uint8_t h = y; h < y + height && h < TEXT_HEIGHT; ++h) {
    const uint32_t y_base = static_cast<uint32_t>(h) * CHAR_HEIGHT;
    for (uint8_t w = x; w < x + width && w < TEXT_WIDTH; ++w) {
      const uint8_t char_index = screen_chars[h * TEXT_WIDTH + w];
      if (char_index >= FONT_CHAR_COUNT) {
        continue;
      }
      const uint8_t *glyph = &FONT[char_index][0][0];
      const uint32_t x_base =
          DISPLAY_OFFSET + static_cast<uint32_t>(w) * CHAR_WIDTH;

      for (uint32_t row = 0; row < CHAR_HEIGHT; ++row) {
        const uint32_t py = y_base + row;
        if (py >= DISPLAY_HEIGHT) {
          break;
        }
        uint16_t *dest_row = testFramebuffer + py * DISPLAY_WIDTH;

        for (uint32_t col = 0; col < CHAR_WIDTH; ++col) {
          const uint32_t px = x_base + col;
          if (px >= DISPLAY_WIDTH) {
            break; // CLIPPING
          }
          const uint8_t alpha = glyph[row * CHAR_WIDTH + col];
          if (alpha == 0) {
            dest_row[px] = bg565;
          } else if (alpha == 255) {
            dest_row[px] = fg565;
          } else {
            const uint32_t inv_a = 255U - alpha;
            const uint32_t r = (fg_r * alpha + bg_r * inv_a + 127U) / 255U;
            const uint32_t g = (fg_g * alpha + bg_g * inv_a + 127U) / 255U;
            const uint32_t b = (fg_b * alpha + bg_b * inv_a + 127U) / 255U;
            dest_row[px] = static_cast<uint16_t>(((b >> 3U) << 11U) |
                                                 ((g >> 2U) << 5U) | (r >> 3U));
          }
        }
      }
    }
  }

  testDirty = true;
}
}

TEST_CASE("Shared Display Renderer: color conversion rgb565 to abgr8888") {
  CHECK(rgb565_to_abgr8888(0x0000) == 0xFF000000);
  CHECK(rgb565_to_abgr8888(0xFFFF) == 0xFFF8FCF8);

  // Pure Red: (0x1F << 11) = 0xF800 -> R is in bits 0..7
  uint32_t red = rgb565_to_abgr8888(0xF800);
  CHECK((red & 0xFF) == 0xF8);
  CHECK(((red >> 8) & 0xFF) == 0x00);
  CHECK(((red >> 16) & 0xFF) == 0x00);

  // Pure Green: (0x3F << 5) = 0x07E0 -> G is in bits 8..15
  uint32_t green = rgb565_to_abgr8888(0x07E0);
  CHECK((green & 0xFF) == 0x00);
  CHECK(((green >> 8) & 0xFF) == 0xFC);
  CHECK(((green >> 16) & 0xFF) == 0x00);

  // Pure Blue: 0x001F -> B is in bits 16..23
  uint32_t blue = rgb565_to_abgr8888(0x001F);
  CHECK((blue & 0xFF) == 0x00);
  CHECK(((blue >> 8) & 0xFF) == 0x00);
  CHECK(((blue >> 16) & 0xFF) == 0xF8);
}

TEST_CASE("Shared Display Renderer: text printing and region batching") {
  ResetTestDisplay();

  display_set_foreground(COLOR_WHITE);
  display_set_background(COLOR_BG);
  display_clear(COLOR_BG);

  CHECK(testDirty);
  CHECK(!recordedRegions.empty());
  ResetTestDisplay();

  // Print text
  display_set_cursor(2, 5);
  display_print("HI", false);

  // Trigger draw_changed
  display_draw_changed();

  // Adjacent characters with the same FG/BG should batch into width 2
  CHECK(recordedRegions.size() == 1);
  CHECK(recordedRegions[0].x == 2);
  CHECK(recordedRegions[0].y == 5);
  CHECK(recordedRegions[0].width == 2);
  CHECK(recordedRegions[0].height == 1);
}

TEST_CASE("Shared Display Renderer: fill_rect boundary clipping") {
  ResetTestDisplay();

  // Draw rectangle within bounds
  display_fill_rect(COLOR_WHITE, 10, 10, 20, 20);
  CHECK(testDirty);

  // Draw rectangle exceeding screen width (720)
  testDirty = false;
  display_fill_rect(COLOR_WHITE, 700, 10, 50, 20); // 700 + 50 = 750 > 720
  CHECK(testDirty);
  // Verify it didn't crash or overflow row 10
  CHECK(testFramebuffer[10 * DISPLAY_WIDTH + 719] != 0);

  // Draw completely out of bounds
  testDirty = false;
  display_fill_rect(COLOR_WHITE, 750, 800, 10, 10);
  CHECK(!testDirty);
}

TEST_CASE("Shared Display Renderer: rightmost column 32 safe clipping at 720") {
  ResetTestDisplay();

  // Column 32 starts at 8 + 32*22 = 712.
  // 712 + 22 = 734 px. The framebuffer is 720 px wide.
  display_set_cursor(32, 0);
  display_putc('A', false);
  display_draw_changed();

  CHECK(recordedRegions.size() == 1);
  CHECK(recordedRegions[0].x == 32);
  CHECK(recordedRegions[0].y == 0);

  // Verify pixels in column 32 (712..719) are drawn across character rows
  bool hasDrawnPixels = false;
  for (uint32_t y = 0; y < CHAR_HEIGHT; ++y) {
    for (int x = 712; x < 720; ++x) {
      if (testFramebuffer[y * DISPLAY_WIDTH + x] != 0) {
        hasDrawnPixels = true;
      }
    }
  }
  CHECK(hasDrawnPixels);

  // Left margin of each row (pixels 0..7) must remain untouched (0),
  // proving that pixels past 719 were clipped and did not wrap into the next
  // line.
  for (uint32_t y = 0; y <= CHAR_HEIGHT; ++y) {
    for (int x = 0; x < 8; ++x) {
      CHECK(testFramebuffer[y * DISPLAY_WIDTH + x] == 0);
    }
  }
}
