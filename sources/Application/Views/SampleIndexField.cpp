/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "SampleIndexField.h"

#include "Application/AppWindow.h"
#include "BaseClasses/ViewUtils.h"
#include "UIFramework/Interfaces/I_GUIGraphics.h"
#include <System/Console/nanoprintf.h>
#include <strings.h>

SampleIndexField::SampleIndexField(const GUIPoint &position, Variable &variable,
                                   int32_t maximum, ChangeRequest changeRequest)
    : UIIntVarField(position, variable, "Sample: %.18s", 0, maximum, 1, 0x10),
      changeRequest_(changeRequest) {}

void SampleIndexField::Draw(GUIWindow &w, int offset) {
  if (src_.GetType() != Variable::CHAR_LIST || src_.GetInt() < 0) {
    UIIntVarField::Draw(w, offset);
    return;
  }

  GUIPoint position = GetTextPosition();
  position._y += offset;

  auto value = src_.GetString();
  constexpr size_t wavExtensionLength = 4;
  if (value.size() >= wavExtensionLength &&
      strcasecmp(value.c_str() + value.size() - wavExtensionLength, ".wav") ==
          0) {
    value.resize(value.size() - wavExtensionLength);
  }

  char buffer[MAX_FIELD_WIDTH + 1];
  npf_snprintf(buffer, sizeof(buffer), format_, value.c_str());

  GUITextProperties props;
  props.region_ = GetTextRegion();
  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);
  } else {
    DrawLabeledField(w, position, buffer, props);
  }
}

void SampleIndexField::ProcessArrow(unsigned short mask) {
  int32_t value = src_.GetInt();

  switch (mask) {
  case EPBM_UP:
    value += yOffset_;
    break;
  case EPBM_DOWN:
    value -= yOffset_;
    break;
  case EPBM_LEFT:
    value -= xOffset_;
    break;
  case EPBM_RIGHT:
    value += xOffset_;
    break;
  }

  if (value < min_) {
    value = min_;
  }
  if (value > max_) {
    value = max_;
  }
  requestChange(value);
}

void SampleIndexField::ProcessClear() {
  if (src_.IsModified()) {
    requestChange(0);
  }
}

void SampleIndexField::requestChange(int32_t value) {
  if (value == src_.GetInt() || !changeRequest_(value)) {
    return;
  }

  src_.SetInt(value);
  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(src_.GetID())));
}
