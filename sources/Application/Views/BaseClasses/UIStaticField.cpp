/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "UIStaticField.h"
#include "Application/AppWindow.h"
#include "ViewUtils.h"
#include <string.h>

UIStaticField::UIStaticField(const GUIPoint &position, const char *string,
                             ColorDefinition color)
    : UIField(position), color_(color) {
  string_ = string;
};

void UIStaticField::Draw(GUIWindow &w, int offset) {

  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  ((AppWindow &)w).SetColor(color_);
  w.DrawString(string_, position, props);
};

void UIStaticField::ProcessArrow(unsigned short mask){};

bool UIStaticField::IsStatic() { return true; };
