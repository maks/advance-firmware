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

#include "UIInsertAmountField.h"

#include "Application/AppWindow.h"
#include "Application/Utils/char.h"
#include "ViewUtils.h"
#include <System/Console/nanoprintf.h>

UIInsertAmountField::UIInsertAmountField(const GUIPoint &position,
                                         Variable &amount, Variable &placement,
                                         const char *format, int min, int max,
                                         int yOffset)
    : UIIntVarField(position, amount, format, min, max, 1, yOffset),
      placement_(placement) {}

void UIInsertAmountField::Draw(GUIWindow &w, int offset) {
  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  char buffer[MAX_FIELD_WIDTH + 1];
  char amount[3];
  hex2char(static_cast<unsigned char>(src_.GetInt() + displayOffset_), amount);
  auto placementValue = placement_.GetString();
  const char *placement = placementValue.c_str();
  npf_snprintf(buffer, sizeof(buffer), format_, amount, placement);

  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);
  } else {
    DrawLabeledField(w, position, buffer, props);
  }
}

void UIInsertAmountField::ProcessArrow(unsigned short mask) {
  switch (mask) {
  case EPBM_UP:
    changeAmount(yOffset_);
    break;
  case EPBM_DOWN:
    changeAmount(-yOffset_);
    break;
  case EPBM_LEFT:
    changePlacement(-1);
    break;
  case EPBM_RIGHT:
    changePlacement(1);
    break;
  }
}

void UIInsertAmountField::ProcessEditArrow(unsigned short mask) {
  switch (mask) {
  case EPBM_UP:
    changeAmount(yOffset_);
    break;
  case EPBM_DOWN:
    changeAmount(-yOffset_);
    break;
  case EPBM_LEFT:
    changeAmount(-xOffset_);
    break;
  case EPBM_RIGHT:
    changeAmount(xOffset_);
    break;
  }
}

void UIInsertAmountField::changeAmount(int delta) {
  int value = static_cast<unsigned char>(src_.GetInt()) + delta;
  while (value < 0) {
    value += 0x100;
  }
  while (value > 0xFF) {
    value -= 0x100;
  }

  src_.SetInt(value);
  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(src_.GetID())));
}

void UIInsertAmountField::changePlacement(int delta) {
  int value = placement_.GetInt() + delta;
  if (value < 0) {
    value = 0;
  }
  if (value >= placement_.GetListSize()) {
    value = placement_.GetListSize() - 1;
  }

  placement_.SetInt(value);
  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(placement_.GetID())));
}
