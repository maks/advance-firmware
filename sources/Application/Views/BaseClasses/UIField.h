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

#ifndef _UI_FIELD_H_
#define _UI_FIELD_H_

#include "System/Console/Trace.h"
#include "UIFramework/BasicDatas/GUIPoint.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "View.h"

class UIField {
public:
  UIField(const GUIPoint &position);
  virtual ~UIField();
  virtual void Draw(GUIWindow &w, int offset = 0) = 0;
  virtual void OnClick() = 0; // ENTER pressed
  virtual void ProcessArrow(unsigned short mask) = 0;
  virtual void OnEditClick(){}; // EDIT pressed
  virtual void ProcessEditArrow(unsigned short mask){};
  virtual void ProcessClear(){}; // EDIT+ENTER pressed
  void SetFocus();
  void ClearFocus();
  bool HasFocus();
  void SetPosition(GUIPoint &);
  virtual GUIPoint GetPosition();
  GUIColor GetColor();
  void SetTextRegion(uint8_t region, int8_t originX = 0, int8_t originY = 0) {
    region_ = region;
    textOriginX_ = originX;
    textOriginY_ = originY;
  }
  uint8_t GetTextRegion() const { return region_; }
  GUIPoint GetTextPosition() {
    return GUIPoint(x_ - textOriginX_, y_ - textOriginY_);
  }

  virtual bool IsStatic();

  // True only for fields that expose GetVariableID(). Callers that look a
  // field up by variable id must check this before casting to
  // UIIntVarField: several field types (actions, text, and the FM6 algorithm
  // and parameter fields) derive from UIField directly.
  virtual bool HasVariable() const;

protected:
  uint8_t x_;
  uint8_t y_;
  bool focus_;
  // Navigation coordinates can span regions; rendering always uses local cells.
  uint8_t region_ = TEXT_REGION_BODY;
  int8_t textOriginX_ = 0, textOriginY_ = 0;
};
#endif
