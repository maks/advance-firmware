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

#include "NullView.h"
#include "Application/Model/FirmwareVersion.h"
#include <Application/AppWindow.h>
#include <nanoprintf.h>

NullView::NullView(GUIWindow &w, ViewData *viewData) : View(w, viewData) {}

NullView::~NullView() {}

void NullView::ProcessButtonMask(unsigned short mask, bool pressed){

};

void NullView::DrawView() {

  Clear();

  GUITextProperties props;
  SetColor(CD_HILITE2);

  const FirmwareNumberString buildString = FormatFirmwareVersionNumber();
  GUIPoint pos;
  pos._y = 21;
  pos._x = (32 - buildString.size()) / 2;
  DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, buildString.c_str(),
                   props);
};

void NullView::OnPlayerUpdate(PlayerEventType, unsigned int tick){

};

void NullView::OnFocus(){};
