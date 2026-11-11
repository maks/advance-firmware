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

#ifndef _MIXER_VIEW_H_
#define _MIXER_VIEW_H_

#include "Application/Effects/OutputEffectConstants.h"
#include "BaseClasses/UIIntVarField.h"
#include "FieldView.h"
#include "ViewData.h"

class MixerView : public FieldView {
public:
  // The normal body starts below the two small-font information rows.
  GUIPoint GetAnchor() override { return GUIPoint(5, 0); }

  MixerView(GUIWindow &w, ViewData *viewData);
  ~MixerView();
  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();
  virtual void AnimationUpdate();
  virtual void SetFocus(UIField *field); // Override to keep songX_ in sync

protected:
  void processNormalButtonMask(unsigned int mask);
  void processSelectionButtonMask(unsigned int mask);
  void onStart(){};
  void onStop(){};
  void updateCursor(int dx, int dy);

  void unMuteAll();
  void toggleMute();
  void switchSoloMode();
  void togglePlay();

private:
  // FX1 and FX2 remain reserved but are hidden until they have effects.
  static constexpr int MixerEffectRowCount =
      OUTPUT_EFFECT_COUNT - kOutputEffectDelayIndex;
  // Volume, the effect sends and the live line input level
  static constexpr int MixerRowCount = 2 + MixerEffectRowCount;
  static constexpr int MixerStripCount = SONG_CHANNEL_COUNT + 1;

  void drawChannelVUMeters(etl::array<stereosample, SONG_CHANNEL_COUNT> *levels,
                           Player *player, GUITextProperties props,
                           bool forceRedraw = false);
  void initMixerFields();
  void drawLineInVuMeter(GUITextProperties props, bool forceRedraw);
  void drawMixerLabels(Player *player, GUITextProperties props);
  UIField *GetSelectedField();
  void syncFocusToSelection();

  etl::vector<UIIntVarField, SONG_CHANNEL_COUNT> channelVolumeFields_;
  etl::vector<UIIntVarField, SONG_CHANNEL_COUNT * OUTPUT_EFFECT_COUNT>
      channelSendFields_;
  etl::vector<UIIntVarField, OUTPUT_EFFECT_COUNT> fxReturnFields_;
  etl::vector<UIIntVarField, 1> masterVolumeField_;
  etl::vector<UIIntVarField, 1> lineInMixField_;
  UIField *fieldGrid_[MixerStripCount][MixerRowCount];
  int selectedStrip_;
  int selectedRow_;
  bool shouldDrawPlayTime() const override { return true; }
};
#endif
