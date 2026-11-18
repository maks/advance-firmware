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

#ifndef _UI_TEXT_FIELD_
#define _UI_TEXT_FIELD_

#include "Foundation/Observable.h"
#include "UIField.h"
#include "stdint.h"

#define MAX_UITEXTFIELD_LABEL_LENGTH 9

template <uint8_t MaxLength>
class UITextField : public UIField, public Observable {
public:
  UITextField(Variable &v, const GUIPoint &position,
              const etl::string<MAX_UITEXTFIELD_LABEL_LENGTH> &label,
              uint8_t fourcc, const etl::string<MaxLength> &defaultValue_,
              bool clearDefaultOnFirstEdit = false);

  virtual ~UITextField();
  void Draw(GUIWindow &w, int offset = 0);
  void ProcessArrow(unsigned short mask);
  void OnClick();
  void OnEditClick();
  void ProcessClear() override;
  etl::string<MaxLength> GetString();
  bool GetKeyboardText(etl::string<MAX_VARIABLE_STRING_LENGTH> &text,
                       uint8_t &maxLength) override;
  void SetKeyboardText(const char *text) override;

  // Set the variable this UITextField is bound to
  void SetVariable(Variable &v);

private:
  int selected_;
  uint8_t currentChar_ = 0;
  uint8_t lastUsedChar_ = 'A';
  Variable *src_; // Pointer instead of reference
  const etl::string<MAX_UITEXTFIELD_LABEL_LENGTH> label_;
  uint8_t fourcc_;
  etl::string<MaxLength> defaultValue_;
  bool clearDefaultOnFirstEdit_;
};

#include "UITextField.ipp"

#endif
