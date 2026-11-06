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

#ifndef _CREDITS_VIEW_H_
#define _CREDITS_VIEW_H_

#include <cstdint>

#include "Application/Views/BaseClasses/View.h"
#include "Application/Views/ModalDialogs/CreditsText.h"

// Number of text lines visible in the credits window (lines 1..20).
static const int32_t kCreditsVisibleLines = 20;

class CreditsView : public View {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 704, 690, TEXT_FONT_NORMAL},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  CreditsView(GUIWindow &w, ViewData *data);
  virtual ~CreditsView();

  virtual void ProcessButtonMask(unsigned short mask, bool pressed) override;
  virtual void DrawView() override;
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int) override {}
  virtual void OnFocus() override;
  virtual void AnimationUpdate() override;

private:
  void scroll(int32_t delta);

  int32_t scrollOffset_ = 0;
};
#endif
