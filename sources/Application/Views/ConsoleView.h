/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include "BaseClasses/View.h"
#include "System/Console/Trace.h"

#define CONSOLE_WIDTH 40
#define CONSOLE_HEIGHT 25

class ConsoleView : public View, public Trace {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 704, 690, TEXT_FONT_NORMAL},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  ConsoleView(GUIWindow &w, ViewData *viewData);

  // View implementation

  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
  virtual void OnFocus(){};

  // Trace Implementation

  virtual void AddBuffer(char *buffer);

  // Additional

  bool IsDirty();

private:
  char lines_[CONSOLE_HEIGHT][CONSOLE_WIDTH];
  int currentLine_;
};

#endif
