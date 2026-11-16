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

#ifndef _FIELD_VIEW_H_
#define _FIELD_VIEW_H_

#include "BaseClasses/UIField.h"
#include "Externals/etl/include/etl/list.h"
#include "ScreenView.h"

class FieldView : public ScreenView {
public:
  FieldView(GUIWindow &w, ViewData *viewData);

  virtual void Redraw();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed) override;

  void SetFocus(UIField *);
  UIField *GetFocus();
  void ClearFocus();
  int GetFocusIndex();
  void SetSize(int size);

  etl::list<UIField *, 64> fieldList_; // adjust to maximum fields on one screen
  // ThemeView currently biggest user: uses 64 (12 colors * 5 + font + theme
  // name + buttons)

private:
  void OnKeyboardTextEntered(View &, ModalView &dialog);
  UIField *focus_;
};

#endif
