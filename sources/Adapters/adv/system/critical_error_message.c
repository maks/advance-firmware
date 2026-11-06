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
#include "critical_error_message.h"
#include "Adapters/adv/display/display.h"
#include "assets/error_bitmaps.h"
#include "charger.h"
#include "stm32h7xx_hal.h"
#include "tim.h"
#include <System/Console/nanoprintf.h>
#include <string.h>

#define ERROR_SCREEN_TEXT_COLUMNS 32

// This function assumes TIM2 is configured to have a 1MHz clock (1us tick)
// Need to use this instead of HAL_Delay() as critical_error_message() can get
// called in early boot before time for Hal_Delay is configured
static void delay_us(uint32_t us) {
  uint32_t start = __HAL_TIM_GET_COUNTER(&htim2);
  while ((__HAL_TIM_GET_COUNTER(&htim2) - start) < us) {
    // Loop until the desired number of microseconds have passed.
    // This handles counter wrapping correctly because of unsigned arithmetic.
  }
}

static void delay_ms(uint32_t ms) { delay_us(ms * 1000); }

static void draw_error_screen_text_line(uint8_t row, uint8_t fgColor,
                                        const char *text) {
  char buffer[ERROR_SCREEN_TEXT_COLUMNS + 1];

  if (!text || row >= TEXT_HEIGHT) {
    return;
  }

  npf_snprintf(buffer, sizeof(buffer), "%s", text);
  buffer[ERROR_SCREEN_TEXT_COLUMNS] = '\0';

  display_set_foreground((color_t)fgColor);
  for (uint8_t col = 0; col < ERROR_SCREEN_TEXT_COLUMNS && buffer[col] != '\0';
       col++) {
    display_putc_region(row == 0 ? 0 : 1, col, row == 0 ? 0 : row - 1,
                        buffer[col], false);
  }
}

static void draw_error_screen(const ErrorBitmap *bitmap, const char *title,
                              const char *const *detailLines,
                              size_t detailLineCount) {
  uint8_t currentRow = 0;

  display_set_palette_color(COLOR_WHITE, 0xFFFF);    // WHITE
  display_set_palette_color(COLOR_GURU_TXT, 0xF800); // RED
  display_set_palette_color(COLOR_GURU_BG, 0x0000);  // BLACK
  display_set_background(COLOR_WHITE);
  display_set_foreground(COLOR_GURU_BG);
  // Fault handling owns an independent emergency layout. It must work before
  // application views exist, and cannot rely on the layout of the failed view.
  static const TextRegion errorRegions[] = {
      {8, 4, 704, 24, TEXT_FONT_SMALL}, {8, 30, 704, 690, TEXT_FONT_NORMAL}};
  display_configure_text_regions(errorRegions, 2);
  display_configure_graphics((GraphicsLayout){NULL, 0});
  display_clear(COLOR_WHITE);

  if (bitmap) {
    display_draw_mono_bitmap_rgb565(0, 0, bitmap->data, bitmap->width,
                                    bitmap->height, bitmap->rowBytes, 0x0000,
                                    0xFFFF);
  }

  if (title || detailLineCount > 0) {
    size_t overlayRows = detailLineCount + ((title != NULL) ? 1U : 0U);

    if (overlayRows > TEXT_HEIGHT) {
      overlayRows = TEXT_HEIGHT;
    }

    // Keep the top rows solid white so the fault text stays legible over the
    // illustration regardless of the art in the bitmap.
    display_fill_rect(COLOR_WHITE, 0, 0, DISPLAY_WIDTH,
                      (uint16_t)(overlayRows * CHAR_HEIGHT));
  }

  if (title) {
    draw_error_screen_text_line(currentRow++, COLOR_GURU_TXT, title);
  }

  for (size_t i = 0; i < detailLineCount && currentRow < TEXT_HEIGHT; i++) {
    draw_error_screen_text_line(currentRow++, COLOR_GURU_TXT, detailLines[i]);
  }

  if (title || detailLineCount > 0) {
    display_draw_changed();
  }
}

static __attribute__((noreturn)) void wait_then_poweroff(int shutdownDelay) {
  for (;;) {
    delay_ms(1000);
    if (shutdownDelay-- < 0) {
      powerOff();
    }
  }
}

// show this message and then power down after the given delay (in seconds) so
// only for critical errors where we need to show user a message and cannot
// continue until a reboot
__attribute__((noreturn)) void
critical_error_message(const ErrorBitmap *bitmap, const char *title,
                       const char *const *detailLines, size_t detailLineCount,
                       int shutdownDelay) {
  draw_error_screen(bitmap, title, detailLines, detailLineCount);
  wait_then_poweroff(shutdownDelay);
}

void critical_sdcard_missing_message(int shutdownDelay) {
  draw_error_screen(&g_sdCardMissingBitmap, NULL, NULL, 0);
  wait_then_poweroff(shutdownDelay);
}
