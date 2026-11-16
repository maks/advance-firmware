/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 xiphonics, inc.
 */
#ifndef KEYBOARD_MODAL_VIEW_H
#define KEYBOARD_MODAL_VIEW_H

#include "Application/Views/BaseClasses/ModalView.h"
#include "Foundation/Variables/Variable.h"

class KeyboardModalView : public ModalView {
public:
  static KeyboardModalView *Create(View &view, const char *title,
                                   const char *text, uint8_t maxLength,
                                   bool projectName = false);
  void Destroy() override;
  void DrawView() override;
  void ProcessButtonMask(unsigned short mask, bool pressed) override;
  void OnFocus() override {}
  void AnimationUpdate() override {}
  void OnPlayerUpdate(PlayerEventType, unsigned int) override {}

  const etl::string<MAX_VARIABLE_STRING_LENGTH> &GetText() const {
    return text_;
  }

private:
  KeyboardModalView(View &view, const char *title, const char *text,
                    uint8_t maxLength, bool projectName);
  void ActivateKey();
  bool IsValidProjectName() const;

  etl::string<28> title_;
  etl::string<MAX_VARIABLE_STRING_LENGTH> text_;
  uint8_t maxLength_;
  bool projectName_;
  etl::string<28> error_;
  uint8_t cursor_;
  uint8_t row_ = 0;
  uint8_t column_ = 0;
  bool symbols_ = false;
  bool lowercase_ = false;
  static bool inUse_;
};

#endif
