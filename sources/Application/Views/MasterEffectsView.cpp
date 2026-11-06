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

#include "MasterEffectsView.h"
#include "Application/Effects/DattorroReverbOutputEffect.h"
#include "Application/Effects/DelayOutputEffect.h"
#include "Application/Effects/OutputEffect.h"
#include "Application/Effects/OutputEffectDefinitions.h"
#include "Application/Player/Player.h"
#include "BaseClasses/ViewUtils.h"
#include "System/Console/n_assert.h"

namespace {

constexpr int32_t DelayColumnX = 0;
constexpr int32_t ReverbColumnX = 14;
constexpr int32_t EffectHeaderY = 0;
constexpr int32_t ParameterBaseY = 1;

} // namespace

MasterEffectsView::MasterEffectsView(GUIWindow &w, ViewData *viewData)
    : FieldView(w, viewData) {
  initFields();
}

MasterEffectsView::~MasterEffectsView() {}

void MasterEffectsView::Reset() {
  ClearFocus();
  fieldList_.clear();
  delayParameterFields_.clear();
  reverbParameterFields_.clear();
  initFields();
}

void MasterEffectsView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed) {
    SetDirty(true);
    return;
  }

  if ((mask & EPBM_EDIT) && (mask & EPBM_PLAY)) {
    if (!Player::GetInstance()->IsRunning()) {
      switchToRecordView();
    }
    return;
  }

  if (mask & EPBM_PLAY) {
    togglePlay();
    return;
  }

  if ((mask & EPBM_NAV) && (mask & EPBM_UP)) {
    ViewType vt = VT_MIXER;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }

  if (mask & EPBM_NAV) {
    return;
  }

  if (!fieldList_.empty()) {
    FieldView::ProcessButtonMask(mask, pressed);
  }
}

void MasterEffectsView::DrawView() {
  Clear();
  setTitle(etl::string<23>("Master Effects"));
  drawEffectHeaders();
  FieldView::Redraw();
  GUITextProperties props;
  drawMasterVuMeter(Player::GetInstance(), props, true);
  drawMap();
}

void MasterEffectsView::OnPlayerUpdate(PlayerEventType eventType,
                                       unsigned int tick) {
  ScreenView::OnPlayerUpdate(eventType, tick);
}

void MasterEffectsView::OnFocus() {
  setTitle(etl::string<23>("Master Effects"));
  if (!GetFocus() && !fieldList_.empty()) {
    SetFocus(*fieldList_.begin());
  }
}

void MasterEffectsView::AnimationUpdate() {
  ScreenView::AnimationUpdate();
  GUITextProperties props;
  drawMasterVuMeter(Player::GetInstance(), props);
  w_.Flush();
}

void MasterEffectsView::initFields() {
  Project *project = viewData_ ? viewData_->project_ : nullptr;
  if (!project) {
    return;
  }

  delayParameterFields_.clear();
  reverbParameterFields_.clear();

  OutputEffect *reverb = project->GetOutputEffect(kOutputEffectReverbIndex);
  OutputEffect *delay = project->GetOutputEffect(kOutputEffectDelayIndex);
  NAssert(reverb != nullptr);
  NAssert(delay != nullptr);
  if (!reverb || !delay) {
    return;
  }

  fillDelayFields(*delay);
  fillReverbFields(*reverb);

  NAssert(delayParameterFields_.size() == DelayParameterFieldCount);
  NAssert(reverbParameterFields_.size() == ReverbParameterFieldCount);
  if (!delayParameterFields_.empty()) {
    SetFocus(&delayParameterFields_.front());
  }
}

void MasterEffectsView::fillReverbFields(OutputEffect &effect) {
  Variable *preDelay = effect.FindVariable(FourCC::VarReverbPreDelay);
  NAssert(preDelay != nullptr);
  reverbParameterFields_.emplace_back(
      GUIPoint(ReverbColumnX, ParameterBaseY), *preDelay, "Pre-delay:  %2.2X",
      DattorroReverbOutputEffect::MinimumParameterValue,
      DattorroReverbOutputEffect::MaximumParameterValue, 1, 0x10);
  fieldList_.push_back(&reverbParameterFields_.back());

  Variable *inputTone = effect.FindVariable(FourCC::VarReverbInputTone);
  NAssert(inputTone != nullptr);
  reverbParameterFields_.emplace_back(
      GUIPoint(ReverbColumnX, ParameterBaseY + 1), *inputTone,
      "Input tone: %2.2X", DattorroReverbOutputEffect::MinimumParameterValue,
      DattorroReverbOutputEffect::MaximumParameterValue, 1, 0x10);
  fieldList_.push_back(&reverbParameterFields_.back());

  Variable *diffuse = effect.FindVariable(FourCC::VarReverbDiffuse);
  NAssert(diffuse != nullptr);
  reverbParameterFields_.emplace_back(
      GUIPoint(ReverbColumnX, ParameterBaseY + 2), *diffuse,
      "Diffuse:    %2.2X", DattorroReverbOutputEffect::MinimumParameterValue,
      DattorroReverbOutputEffect::MaximumParameterValue, 1, 0x10);
  fieldList_.push_back(&reverbParameterFields_.back());

  Variable *decay = effect.FindVariable(FourCC::VarReverbDecay);
  NAssert(decay != nullptr);
  reverbParameterFields_.emplace_back(
      GUIPoint(ReverbColumnX, ParameterBaseY + 3), *decay, "Decay time: %2.2X",
      DattorroReverbOutputEffect::MinimumParameterValue,
      DattorroReverbOutputEffect::MaximumParameterValue, 1, 0x10);
  fieldList_.push_back(&reverbParameterFields_.back());

  Variable *tailTone = effect.FindVariable(FourCC::VarReverbTailTone);
  NAssert(tailTone != nullptr);
  reverbParameterFields_.emplace_back(
      GUIPoint(ReverbColumnX, ParameterBaseY + 4), *tailTone,
      "Tail tone:  %2.2X", DattorroReverbOutputEffect::MinimumParameterValue,
      DattorroReverbOutputEffect::MaximumParameterValue, 1, 0x10);
  fieldList_.push_back(&reverbParameterFields_.back());

  Variable *modulationRate =
      effect.FindVariable(FourCC::VarReverbModulationRate);
  NAssert(modulationRate != nullptr);
  reverbParameterFields_.emplace_back(
      GUIPoint(ReverbColumnX, ParameterBaseY + 5), *modulationRate,
      "Mod speed:  %2.2X", DattorroReverbOutputEffect::MinimumParameterValue,
      DattorroReverbOutputEffect::MaximumParameterValue, 1, 0x10);
  fieldList_.push_back(&reverbParameterFields_.back());

  Variable *modulationDepth =
      effect.FindVariable(FourCC::VarReverbModulationDepth);
  NAssert(modulationDepth != nullptr);
  reverbParameterFields_.emplace_back(
      GUIPoint(ReverbColumnX, ParameterBaseY + 6), *modulationDepth,
      "Mod level:  %2.2X", DattorroReverbOutputEffect::MinimumParameterValue,
      DattorroReverbOutputEffect::MaximumParameterValue, 1, 0x10);
  fieldList_.push_back(&reverbParameterFields_.back());
}

void MasterEffectsView::fillDelayFields(OutputEffect &effect) {
  Project &project = *viewData_->project_;

  Variable *time = effect.FindVariable(FourCC::VarDelayTime);
  NAssert(time != nullptr);
  delayParameterFields_.emplace_back(
      GUIPoint(DelayColumnX, ParameterBaseY), *time, "Time:     %2.2d",
      DelayOutputEffect::MinimumTime, DelayOutputEffect::MaximumTime, 1,
      DelayOutputEffect::TicksPerStep, 0);
  fieldList_.push_back(&delayParameterFields_.back());

  Variable *feedback = effect.FindVariable(FourCC::VarDelayFeedback);
  NAssert(feedback != nullptr);
  delayParameterFields_.emplace_back(
      GUIPoint(DelayColumnX, ParameterBaseY + 1), *feedback, "Feedback: %2.2X",
      0, DelayOutputEffect::MaximumFeedback, 1, 0x10);
  fieldList_.push_back(&delayParameterFields_.back());

  Variable *reverbSend = project.FindVariable(GetOutputEffectRouteVariableId(
      kOutputEffectDelayIndex, kOutputEffectReverbIndex));
  NAssert(reverbSend != nullptr);
  delayParameterFields_.emplace_back(GUIPoint(DelayColumnX, ParameterBaseY + 2),
                                     *reverbSend, "RVB send: %2.2X", 0,
                                     OutputEffectSendMaximum, 1, 0x10);
  fieldList_.push_back(&delayParameterFields_.back());
}

void MasterEffectsView::drawEffectHeaders() {
  GUITextProperties properties;
  SetColor(CD_HILITE2);
  DrawRegionString(TEXT_REGION_BODY, DelayColumnX, EffectHeaderY, "Delay",
                   properties);
  DrawRegionString(TEXT_REGION_BODY, ReverbColumnX, EffectHeaderY, "Reverb",
                   properties);
}

void MasterEffectsView::togglePlay() {
  if (!viewData_ || !viewData_->song_) {
    return;
  }

  const int32_t songChannelCount = SONG_CHANNEL_COUNT;
  if (songChannelCount <= 0) {
    return;
  }

  Player *player = Player::GetInstance();
  player->OnSongStartButton(0, static_cast<uint32_t>(songChannelCount - 1),
                            false, false);
}
