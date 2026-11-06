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

#ifndef _MASTER_EFFECTS_VIEW_H_
#define _MASTER_EFFECTS_VIEW_H_

#include "BaseClasses/UIIntVarField.h"
#include "FieldView.h"
#include "ViewData.h"

#include <cstddef>
#include <cstdint>

class OutputEffect;

class MasterEffectsView : public FieldView {
public:
  // The normal body starts below the two small-font information rows.
  GUIPoint GetAnchor() override { return GUIPoint(5, 0); }

  MasterEffectsView(GUIWindow &w, ViewData *viewData);
  ~MasterEffectsView();

  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();
  virtual void AnimationUpdate();

private:
  static constexpr size_t DelayParameterFieldCount = 3;
  static constexpr size_t ReverbParameterFieldCount = 7;

  void initFields();
  void fillReverbFields(OutputEffect &effect);
  void fillDelayFields(OutputEffect &effect);
  void drawEffectHeaders();
  void togglePlay();
  bool shouldDrawPlayTime() const override { return true; }

  etl::vector<UIIntVarField, DelayParameterFieldCount> delayParameterFields_;
  etl::vector<UIIntVarField, ReverbParameterFieldCount> reverbParameterFields_;
};

#endif
