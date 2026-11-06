/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "ModalView.h"
#include "Application/AppWindow.h"

uint32_t ModalView::nextInstanceId_ = 0;

ModalView::ModalView(View &v)
    : View(v.w_, v.viewData_), instanceId_(++nextInstanceId_), finished_(false),
      returnCode_(0){};

ModalView::~ModalView(){};

int ModalView::GetReturnCode() { return returnCode_; };
uint32_t ModalView::GetInstanceId() const { return instanceId_; };

bool ModalView::IsFinished() { return finished_; };

void ModalView::EndModal(int returnCode) {
  returnCode_ = returnCode;
  finished_ = true;
};

void ModalView::Destroy() { delete this; }

void ModalView::ClearTextRect(int x, int y, int w, int h) {
  GUIRect rect(x + left_, y + top_, x + left_ + w, y + top_ + h);
  w_.ClearTextRect(rect);
}
void ModalView::DrawString(int x, int y, const char *txt,
                           const GUITextProperties &props) {
  w_.DrawString(txt, GUIPoint(x + left_, y + top_), props);
};

GUIPoint ModalView::GetAnchor() {
  // Get the base anchor point from View
  GUIPoint baseAnchor = View::GetAnchor();

  // Adjust for modal window position
  baseAnchor._x = left_;
  baseAnchor._y = top_;

  return baseAnchor;
}

void ModalView::SetWindow(int width, int height) {

  if (width > 28) {
    width = 28;
  };
  if (height > 20) {
    height = 20;
  };

  left_ = 16 - width / 2;
  top_ = 8 - height / 2;
  if (top_ < 2) {
    top_ = 2;
  }
  static_cast<AppWindow &>(w_).SetOverlayBounds(
      {int16_t(8 + (left_ - 2) * 22), int16_t((top_ - 2) * 30),
       int16_t((width + 4) * 22), int16_t((height + 4) * 30)});
  ClearTextRect(-1, -1, width + 2, height + 2);

  SetColor(CD_ACCENT);
  GUITextProperties props;
  props.invert_ = false;
  // Native FONT box-drawing glyphs. Keep the existing border cell bounds so
  // overlay capture and restoration cover the same rectangle as before.
  char line[SCREEN_WIDTH + 1];
  memset(line, 196, width + 4);
  line[width + 4] = '\0';
  line[0] = static_cast<char>(218);
  line[width + 3] = static_cast<char>(191);
  DrawString(-2, -2, line, props);
  line[0] = static_cast<char>(192);
  line[width + 3] = static_cast<char>(217);
  DrawString(-2, height + 1, line, props);
  line[0] = static_cast<char>(179);
  line[1] = '\0';
  for (int i = 0; i < height + 2; i++) {
    DrawString(-2, i - 1, line, props);
    DrawString(width + 1, i - 1, line, props);
  }
};
