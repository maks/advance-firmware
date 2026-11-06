/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#include "UIFramework/BasicDatas/TextFont.h"
#include "font.h"
// Derive metrics from the assets once. The renderer has no font-specific paths.
#define FONT_INFO(asset)                                                       \
  {                                                                            \
    &asset[0][0][0], sizeof(asset) / sizeof(asset[0]), sizeof(asset[0][0]),    \
        sizeof(asset[0]) / sizeof(asset[0][0])                                 \
  }
const TextFontInfo text_fonts[TEXT_FONT_COUNT] = {
    FONT_INFO(FONT), FONT_INFO(FONTSMALL), FONT_INFO(FONTTINY)};
