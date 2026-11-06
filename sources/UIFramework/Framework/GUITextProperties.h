/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _GUI_TEXT_PROPERTIES_H_
#define _GUI_TEXT_PROPERTIES_H_

#include "UIFramework/BasicDatas/TextRegionLayout.h"

struct GUITextProperties {
  GUITextProperties() : invert_(false){};
  GUITextProperties(bool inverted) : invert_(inverted){};
  bool invert_;
  // A region must be supplied for base text. Overlays use their fixed grid.
  uint8_t region_ = UINT8_MAX;
};

#endif
