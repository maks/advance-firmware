/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "UIPitchOffsetVarField.h"

#include "Application/AppWindow.h"
#include "ViewUtils.h"
#include <cstring>

UIPitchOffsetVarField::UIPitchOffsetVarField(const GUIPoint &position,
                                             Variable &variable)
    : UIIntVarField(position, variable, "%d", PitchOffset::MinimumCents,
                    PitchOffset::MaximumCents, 1,
                    PitchOffset::CentsPerSemitone) {}

void UIPitchOffsetVarField::Draw(GUIWindow &window, int offset) {
  GUITextProperties properties;
  properties.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  const auto value =
      UIPitchOffsetVarFieldDetail::FormatPitchOffsetCents(src_.GetInt());
  char buffer[UIPitchOffsetVarFieldDetail::DisplayWidth + 1];
  memcpy(buffer, value.c_str(), value.size());
  buffer[value.size()] = '\0';

  if (focus_) {
    static_cast<AppWindow &>(window).SetColor(CD_HILITE2);
    properties.invert_ = true;
    window.DrawString(buffer, position, properties);
  } else {
    DrawLabeledField(window, position, buffer, properties);
  }
}
