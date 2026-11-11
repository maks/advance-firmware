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

#include "UIBigHexVarField.h"
#include "Application/AppWindow.h"
#include "ViewUtils.h"
#include <System/Console/nanoprintf.h>
#include <string.h>

UIBigHexVarField::UIBigHexVarField(const GUIPoint &position, Variable &v,
                                   int precision, const char *format, int min,
                                   int max, int power, bool wrap,
                                   UIBigHexEditMode editMode)
    : UIIntVarField(position, v, format, min, max, 0, 0) {
  precision_ = precision - 1;
  power_ = power;
  position_ = 0;
  wrap_ = wrap;
  editMode_ = editMode;
};

void UIBigHexVarField::Draw(GUIWindow &w, int offset) {

  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  char buffer[MAX_FIELD_WIDTH + 1];
  int value = src_.GetInt();
  npf_snprintf(buffer, sizeof(buffer), format_, value);

  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);

    int percentPos = -1;
    for (unsigned int i = 0; i < strlen(format_); i++) {
      if (format_[i] == '%') {
        percentPos = i;
        break;
      };
    };
    if (percentPos >= 0) {
      int offset = (precision_ - position_) + percentPos;
      buffer[offset + 1] = 0;
      position._x += offset;
      ((AppWindow &)w).SetColor(CD_NORMAL);
      w.DrawString(buffer + offset, position, props);
    }
  } else {
    DrawLabeledField(w, position, buffer, props);
  }
};

void UIBigHexVarField::ResetDigitPosition() { position_ = 0; }

// ApplyOffset is used for selection editing where callers need to directly
// change the value without moving the focused digit.
// ProcessArrow is not suitable because it treats LEFT/RIGHT as digit-
// position navigation (changing which hex digit UP/DOWN modifies) rather
// than value change, and when position_ is 0 UP/DOWN only changes by 1.
void UIBigHexVarField::ApplyOffset(int offset) {
  const int previous = src_.GetInt();
  int value = previous + offset;
  if (value > max_) {
    value = (wrap_) ? value - max_ + min_ - 1 : max_;
  };
  if (value < min_) {
    value = (wrap_) ? max_ + (value - min_) + 1 : min_;
  };
  if (value == previous) {
    return;
  }

  src_.SetInt(value);
  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(src_.GetID())));
};

void UIBigHexVarField::ProcessArrow(unsigned short mask) {

  const int previous = src_.GetInt();
  int value = previous;
  int offset = 1;
  for (unsigned int i = 0; i < position_; i++) {
    offset *= power_;
  }

  switch (mask) {
  case EPBM_LEFT:
    if (position_ < precision_) {
      position_++;
    };
    break;
  case EPBM_RIGHT:
    if (position_ > 0) {
      position_--;
    };
    break;
  case EPBM_UP:
    value = editMode_ == UIBigHexEditMode::IndependentDigits
                ? UIBigHexVarFieldDetail::AdjustIndependentHexDigit(
                      value, position_, 1)
                : value + offset;
    break;

  case EPBM_DOWN:
    value = editMode_ == UIBigHexEditMode::IndependentDigits
                ? UIBigHexVarFieldDetail::AdjustIndependentHexDigit(
                      value, position_, -1)
                : value - offset;
    break;
  };
  if (value > max_) {
    value = (wrap_) ? value - max_ + min_ - 1 : max_;
  };
  if (value < min_) {
    value = (wrap_) ? max_ + (value - min_) + 1 : min_;
  };
  if (value == previous) {
    return;
  }

  src_.SetInt(value);
  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(src_.GetID())));
};
