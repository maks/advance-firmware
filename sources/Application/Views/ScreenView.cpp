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

#include "ScreenView.h"
#include <Application/AppWindow.h>
#include <nanoprintf.h>

ScreenView::ScreenView(GUIWindow &w, ViewData *viewData) : View(w, viewData) {}

ScreenView::~ScreenView() {}

void ScreenView::OnPlayerUpdate(PlayerEventType, unsigned int tick) {
  (void)tick;
  if (shouldDrawPlayTime()) {
    needsPlayTimeUpdate_ = true;
  }
}

/// Updates the animation by redrawing the battery gauge and power button UI on
/// every clock tick
void ScreenView::AnimationUpdate() {
  GUITextProperties props;
  updateDeferredPlayButtonHold();
  drawTitlebar(TEXT_REGION_HEADER, props);
  const TextRegionInfo *info =
      shouldDrawPlayTime()
          ? static_cast<AppWindow &>(w_).GetTextRegion(TEXT_REGION_INFO)
          : nullptr;
  if (info && info->columns >= 5 && needsPlayTimeUpdate_) {
    const int column = info->columns - 5;
    Player *player = Player::GetInstance();
    if (player != nullptr) {
      char strbuffer[10];
      SetColor(CD_NORMAL);
      props.invert_ = false;
      if (!player->IsRunning()) {
        DrawRegionString(TEXT_REGION_INFO, column, 0, "     ", props);
      } else {
        int time = int(player->GetPlayTime());
        int mi = time / 60;
        int se = time - mi * 60;
        npf_snprintf(strbuffer, sizeof(strbuffer), "%2.2d:%2.2d", mi, se);
        DrawRegionString(TEXT_REGION_INFO, column, 0, strbuffer, props);
      }
    }
    needsPlayTimeUpdate_ = false;
  }
  drawPowerButtonUI(props);
};
