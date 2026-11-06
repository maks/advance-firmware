/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * WASM software A8 display backend for PicoTracker.
 */

#include "System/Display/Display.h"
#include "System/Display/DisplayBackend.h"
#include "System/Display/font.h"
#include "WasmDisplayBackend.h"

#include <cstdint>

extern "C" {
uint16_t *display_backend_get_framebuffer(void) {
  return const_cast<uint16_t *>(WasmDisplayBackend::GetFramebuffer());
}

void display_backend_mark_dirty(void) { WasmDisplayBackend::MarkDirty(); }

void display_backend_draw_region(uint8_t x, uint8_t y, uint8_t width,
                                 uint8_t height, uint32_t bg_color,
                                 uint32_t fg_color,
                                 const uint8_t *screen_chars) {
  uint16_t *fb = display_backend_get_framebuffer();
  if (fb == nullptr || screen_chars == nullptr) {
    return;
  }

  // Extract color components from palette 32-bit colors (0xFF000000 | (B << 16)
  // | (G << 8) | R)
  const uint32_t fg_r = fg_color & 0xFFU;
  const uint32_t fg_g = (fg_color >> 8U) & 0xFFU;
  const uint32_t fg_b = (fg_color >> 16U) & 0xFFU;

  const uint32_t bg_r = bg_color & 0xFFU;
  const uint32_t bg_g = (bg_color >> 8U) & 0xFFU;
  const uint32_t bg_b = (bg_color >> 16U) & 0xFFU;

  const uint16_t fg_color565 = static_cast<uint16_t>(
      ((fg_b >> 3U) << 11U) | ((fg_g >> 2U) << 5U) | (fg_r >> 3U));
  const uint16_t bg_color565 = static_cast<uint16_t>(
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
        uint16_t *dest_row = fb + py * DISPLAY_WIDTH;

        for (uint32_t col = 0; col < CHAR_WIDTH; ++col) {
          const uint32_t px = x_base + col;
          // CRITICAL: Text layout extends to 8 + 33*22 = 734 px.
          // Pixels >= 720 MUST be safely clipped!
          if (px >= DISPLAY_WIDTH) {
            break;
          }

          const uint8_t alpha = glyph[row * CHAR_WIDTH + col];
          if (alpha == 0) {
            dest_row[px] = bg_color565;
          } else if (alpha == 255) {
            dest_row[px] = fg_color565;
          } else {
            const uint32_t inv_alpha = 255U - alpha;
            const uint32_t blended_r =
                (fg_r * alpha + bg_r * inv_alpha + 127U) / 255U;
            const uint32_t blended_g =
                (fg_g * alpha + bg_g * inv_alpha + 127U) / 255U;
            const uint32_t blended_b =
                (fg_b * alpha + bg_b * inv_alpha + 127U) / 255U;

            dest_row[px] = static_cast<uint16_t>(((blended_b >> 3U) << 11U) |
                                                 ((blended_g >> 2U) << 5U) |
                                                 (blended_r >> 3U));
          }
        }
      }
    }
  }

  WasmDisplayBackend::MarkDirty();
}
}
