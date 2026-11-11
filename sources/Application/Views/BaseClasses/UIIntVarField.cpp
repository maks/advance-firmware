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

#include "UIIntVarField.h"

#include "Application/AppWindow.h"
#include "System/Console/Trace.h"
#include "UIFramework/Interfaces/I_GUIGraphics.h"
#include "UIIntVarField.h"
#include "ViewUtils.h"
#include <System/Console/nanoprintf.h>
#include <string.h>

#define abs(x) (x < 0 ? -x : x)

UIIntVarField::UIIntVarField(const GUIPoint &position, Variable &v,
                             const char *format, int min, int max, int xOffset,
                             int yOffset, int displayOffset,
                             UIIntVarFieldDisplayMode displayMode,
                             int displayDivisor)
    : UIField(position), src_(v) {
  format_ = format;
  min_ = min;
  max_ = max;
  xOffset_ = xOffset;
  yOffset_ = yOffset;
  displayOffset_ = displayOffset;
  displayMode_ = displayMode;
  displayDivisor_ = displayDivisor > 0 ? displayDivisor : 1;
};

void UIIntVarField::Draw(GUIWindow &w, int offset) {

  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  Variable::Type type = src_.GetType();
  char buffer[MAX_FIELD_WIDTH + 1];
  switch (type) {
  case Variable::INT: {
    int ivalue = src_.GetInt() + displayOffset_;
    if (displayMode_ == UIIntVarFieldDisplayMode::SignedHex) {
      char value[12];
      if (ivalue < 0) {
        npf_snprintf(value, sizeof(value), "-%X",
                     static_cast<unsigned int>(-ivalue));
      } else if (ivalue > 0) {
        npf_snprintf(value, sizeof(value), "+%X",
                     static_cast<unsigned int>(ivalue));
      } else {
        strcpy(value, "0");
      }
      npf_snprintf(buffer, sizeof(buffer), format_, value);
    } else {
      if (displayDivisor_ == 1) {
        npf_snprintf(buffer, sizeof(buffer), format_, ivalue, ivalue);
      } else {
        const int whole = ivalue / displayDivisor_;
        int fraction = ivalue % displayDivisor_;
        if (fraction < 0) {
          fraction = -fraction;
        }
        npf_snprintf(buffer, sizeof(buffer), format_, whole, fraction);
      }
    }
  } break;
  case Variable::CHAR_LIST:
    // if no value initialize with "NONE"
    if (src_.GetInt() < 0) {
      npf_snprintf(buffer, sizeof(buffer), format_, "NONE");
    } else {
      auto value = src_.GetString();
      const char *cvalue = value.c_str();
      npf_snprintf(buffer, sizeof(buffer), format_, cvalue);
    }
    break;
  case Variable::BOOL: {
    const char *cvalue = src_.GetInt() ? "on" : "off";
    npf_snprintf(buffer, sizeof(buffer), format_, cvalue);
  } break;

  default:
    strcpy(buffer, "++wtf++");
  }

  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);
  } else {
    DrawLabeledField(w, position, buffer, props);
  }
};

void UIIntVarField::ProcessArrow(unsigned short mask) {
  const int previous = src_.GetInt();
  int value = previous;

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
  };
  if (value < min_) {
    value = min_;
  };
  if (value > max_) {
    value = max_;
  }

  if (value == previous) {
    return;
  }

  src_.SetInt(value);
  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(src_.GetID())));
};

void UIIntVarField::ProcessClear() {
  if (!src_.IsModified())
    return;

  src_.Reset();

  SetChanged();
  NotifyObservers(reinterpret_cast<I_ObservableData *>(
      static_cast<uintptr_t>(src_.GetID())));
};

FourCC UIIntVarField::GetVariableID() { return src_.GetID(); };

Variable &UIIntVarField::GetVariable() { return src_; };
