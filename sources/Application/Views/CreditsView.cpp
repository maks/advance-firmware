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

#include "CreditsView.h"
#include "Application/AppWindow.h"

CreditsView::CreditsView(GUIWindow &w, ViewData *data) : View(w, data) {}

CreditsView::~CreditsView() {}

void CreditsView::DrawView() {
  Clear();

  GUITextProperties props;

  // Clamp the scroll offset so we never scroll past the last line.
  int32_t maxOffset = kCreditsLineCount - kCreditsVisibleLines;
  if (maxOffset < 0) {
    maxOffset = 0;
  }
  if (scrollOffset_ > maxOffset) {
    scrollOffset_ = maxOffset;
  }
  if (scrollOffset_ < 0) {
    scrollOffset_ = 0;
  }

  // Draw the visible credits text lines starting at y=1 (2nd line).
  SetColor(CD_NORMAL);
  for (int32_t i = 0; i < kCreditsVisibleLines; i++) {
    int32_t lineIndex = scrollOffset_ + i;
    if (lineIndex >= static_cast<int32_t>(kCreditsLineCount)) {
      break;
    }
    DrawRegionString(TEXT_REGION_BODY, 0, i, kCreditsLines[lineIndex], props);
  }

  // Draw the OK button at the bottom (y=21, one line below the text block).
  static const char *buttonText = "Ok";
  int32_t y = kCreditsVisibleLines;
  int32_t x = (SCREEN_WIDTH - static_cast<int32_t>(strlen(buttonText))) / 2;
  SetColor(CD_HILITE2);
  props.invert_ = true;
  DrawRegionString(TEXT_REGION_BODY, x, y, buttonText, props);
  props.invert_ = false;
}

void CreditsView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed) {
    return;
  }
  if (mask & EPBM_UP) {
    scroll(-1);
  } else if (mask & EPBM_DOWN) {
    scroll(+1);
  } else if (mask & EPBM_ENTER) {
    ViewType vt = VT_DEVICE;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  } else if ((mask & EPBM_LEFT) && (mask & EPBM_NAV)) {
    ViewType vt = VT_DEVICE;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  }
}

void CreditsView::scroll(int32_t delta) {
  scrollOffset_ += delta;
  int32_t maxOffset = kCreditsLineCount - kCreditsVisibleLines;
  if (maxOffset < 0) {
    maxOffset = 0;
  }
  if (scrollOffset_ > maxOffset) {
    scrollOffset_ = maxOffset;
  }
  if (scrollOffset_ < 0) {
    scrollOffset_ = 0;
  }
  isDirty_ = true;
}

void CreditsView::OnFocus() {
  setTitle(etl::string<23>("Credits"));
  isDirty_ = true;
}

void CreditsView::AnimationUpdate() {
  GUITextProperties props;
  drawTitlebar(TEXT_REGION_HEADER, props);
  drawPowerButtonUI(props);
}
