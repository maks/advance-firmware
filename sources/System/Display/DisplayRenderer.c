/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * Shared display renderer implementation for PicoTracker.
 */

#include "System/Display/Display.h"
#include "System/Display/DisplayBackend.h"
#include "System/Display/font.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static color_t screen_bg_color = COLOR_BG;
static color_t screen_fg_color = COLOR_NORMAL;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t screen[TEXT_HEIGHT * TEXT_WIDTH] = {0};
static uint8_t colors[TEXT_HEIGHT * TEXT_WIDTH] = {0};
static uint8_t ui_font_index = 0;

// Bit array to track changed text cells
static uint8_t changed[TEXT_HEIGHT * TEXT_WIDTH / 8] = {0};
#define SetBit(A, k) (A[(k) / 8] |= (1 << ((k) % 8)))
#define ClearBit(A, k) (A[(k) / 8] &= ~(1 << ((k) % 8)))
#define TestBit(A, k) (A[(k) / 8] & (1 << ((k) % 8)))

uint32_t rgb565_to_abgr8888(uint16_t rgb565) {
  uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
  uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
  uint8_t b = (rgb565 & 0x1F) << 3;
  return 0xFF000000 | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

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

void display_set_font_index(uint8_t idx) { ui_font_index = idx; }

void display_set_cursor(uint8_t x, uint8_t y) {
  cursor_x = x;
  cursor_y = y;
}

void display_putc(char c, bool invert) {
  if (cursor_x < 0 || cursor_x >= TEXT_WIDTH || cursor_y < 0 ||
      cursor_y >= TEXT_HEIGHT) {
    return;
  }
  int idx = cursor_y * TEXT_WIDTH + cursor_x;
  // Treat incoming character bytes as unsigned so custom glyphs (0x80-0xFF)
  // are not dropped on toolchains where plain char is signed.
  uint8_t uc = (uint8_t)c;
  if (uc >= 32) {
    screen[idx] = uc - 32;
    SetBit(changed, idx);
    if (invert) {
      colors[idx] = ((screen_bg_color & 0xf) << 4) | (screen_fg_color & 0xf);
    } else {
      colors[idx] = ((screen_fg_color & 0xf) << 4) | (screen_bg_color & 0xf);
    }
  }
}

void display_print(const char *str, bool invert) {
  char c;
  while ((c = *str++)) {
    display_putc(c, invert);
    cursor_x++;
  }
}

void display_set_foreground(color_t color) { screen_fg_color = color; }

void display_set_background(color_t color) { screen_bg_color = color; }

void display_clear(color_t color) {
  int size = TEXT_WIDTH * TEXT_HEIGHT;
  memset(screen, 0, size);
  memset(colors, color, size);
  display_set_cursor(0, 0);
  display_draw_screen();
}

void display_set_palette_color(int idx, uint16_t rgb565_color) {
  if (idx >= 0 && idx < 16) {
    palette[idx] = rgb565_to_abgr8888(rgb565_color);
  }
}

void display_draw_region(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  if (width == 0 || height == 0 || x >= TEXT_WIDTH || y >= TEXT_HEIGHT) {
    return;
  }
  if (x + width > TEXT_WIDTH) {
    width = TEXT_WIDTH - x;
  }
  if (y + height > TEXT_HEIGHT) {
    height = TEXT_HEIGHT - y;
  }
  uint32_t idx = y * TEXT_WIDTH + x;
  uint32_t bg_color = palette[colors[idx] & 0xF];
  uint32_t fg_color = palette[colors[idx] >> 4];

  display_backend_draw_region(x, y, width, height, bg_color, fg_color, screen);
}

void display_draw_sub_region(uint8_t x, uint8_t y, uint8_t width,
                             uint8_t height) {
  display_draw_region(x, y, width, height);
}

void display_draw_screen() {
  display_draw_region(0, 0, TEXT_WIDTH, TEXT_HEIGHT);
}

void display_draw_changed() {
  uint32_t bg_color;
  uint32_t fg_color;
  for (int idx = 0; idx < TEXT_HEIGHT * TEXT_WIDTH; idx++) {
    if (TestBit(changed, idx)) {
      fg_color = palette[colors[idx] >> 4];
      bg_color = palette[colors[idx] & 0xf];

      ClearBit(changed, idx);
      uint16_t y = idx / TEXT_WIDTH;
      uint16_t x = idx - (TEXT_WIDTH * y);

      int height = 1;
      for (int probe_y = y + 1; probe_y < TEXT_HEIGHT; probe_y++) {
        int probe_idx = probe_y * TEXT_WIDTH + x;
        if (TestBit(changed, probe_idx) &&
            fg_color == palette[colors[probe_idx] >> 4] &&
            bg_color == palette[colors[probe_idx] & 0xf]) {
          ClearBit(changed, probe_idx);
          height++;
          continue;
        }
        break;
      }

      int16_t width = 1;
      for (int probe_x = x + 1; probe_x < TEXT_WIDTH; probe_x++) {
        for (int probe_y = y; probe_y < y + height; probe_y++) {
          int probe_idx = probe_y * TEXT_WIDTH + probe_x;
          if (!TestBit(changed, probe_idx) ||
              fg_color != palette[colors[probe_idx] >> 4] ||
              bg_color != palette[colors[probe_idx] & 0xf]) {
            for (int undo_y = y; undo_y < probe_y; undo_y++) {
              SetBit(changed, undo_y * TEXT_WIDTH + probe_x);
            }
            goto end;
          }
          ClearBit(changed, probe_idx);
        }
        width++;
      }
    end:
      display_draw_region(x, y, width, height);
    }
  }
}

void display_fill_rect(uint8_t color_index, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height) {
  if (width == 0 || height == 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }
  if (x + width > DISPLAY_WIDTH) {
    width = DISPLAY_WIDTH - x;
  }
  if (y + height > DISPLAY_HEIGHT) {
    height = DISPLAY_HEIGHT - y;
  }

  uint16_t color565 = abgr8888_to_bgr565(palette[color_index & 0xF]);
  uint16_t *framebuffer = display_backend_get_framebuffer();
  if (framebuffer == NULL) {
    return;
  }

  for (uint16_t j = y; j < y + height; j++) {
    for (uint16_t i = x; i < x + width; i++) {
      framebuffer[j * DISPLAY_WIDTH + i] = color565;
    }
  }
  display_backend_mark_dirty();
}

void display_draw_mono_bitmap(uint16_t x, uint16_t y, const uint8_t *bitmap,
                              uint16_t width, uint16_t height,
                              uint16_t row_bytes, uint8_t fg_color_index,
                              uint8_t bg_color_index) {
  uint16_t fg_color565 = abgr8888_to_bgr565(palette[fg_color_index & 0xF]);
  uint16_t bg_color565 = abgr8888_to_bgr565(palette[bg_color_index & 0xF]);

  display_draw_mono_bitmap_rgb565(x, y, bitmap, width, height, row_bytes,
                                  fg_color565, bg_color565);
}

void display_draw_mono_bitmap_rgb565(uint16_t x, uint16_t y,
                                     const uint8_t *bitmap, uint16_t width,
                                     uint16_t height, uint16_t row_bytes,
                                     uint16_t fg_color565,
                                     uint16_t bg_color565) {
  if (!bitmap || width == 0 || height == 0 || x >= DISPLAY_WIDTH ||
      y >= DISPLAY_HEIGHT) {
    return;
  }
  if (x + width > DISPLAY_WIDTH) {
    width = DISPLAY_WIDTH - x;
  }
  if (y + height > DISPLAY_HEIGHT) {
    height = DISPLAY_HEIGHT - y;
  }

  uint16_t *framebuffer = display_backend_get_framebuffer();
  if (framebuffer == NULL) {
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
  display_backend_mark_dirty();
}
