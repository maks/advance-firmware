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

#include "MixerView.h"
#include "Application/Effects/OutputEffectDefinitions.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Utils/mathutils.h"
#include "Application/Views/SampleEditorView.h"
#include "UIController.h"
#include <Application/AppWindow.h>
#include <algorithm>

namespace {

constexpr int MixerStripX = 5;
constexpr int MixerStripStride = 3;
// VU meters grow upwards from a baseline at (anchorY + height - 1). The strip
// labels sit one row below the baseline and the VOL field one row below that.
// The meters leave room for the permanent FX send rows and the line input row
// above the bottom map.
constexpr int MixerVuMeterHeight = 7;
constexpr int MixerStripLabelY = 7;
constexpr int MixerFieldBaseY = 8;
constexpr int MixerRowLabelX = 1;
constexpr int MixerMasterStripIndex = SONG_CHANNEL_COUNT;
constexpr int MixerMasterVuXOffset = MixerMasterStripIndex * MixerStripStride;
constexpr int MixerVolumeRow = 0;
constexpr int MixerFirstEffectRow = 1;
// The live line input sits below the sends and only ever occupies the channel
// 01 column. Its meter grows upwards from one row under the channel meters, and
// the level value goes underneath it, on the last row of the body.
constexpr int MixerLineInRow = 3;
constexpr int MixerLineInVuRows = 5;
constexpr int MixerLineInVuBaseY = MixerFieldBaseY + MixerVuMeterHeight;
constexpr int MixerLineInValueY = MixerLineInVuBaseY + 1;
// Keep FX1 and FX2 available in the model while hiding their Mixer rows.
constexpr int MixerFirstVisibleEffectIndex = kOutputEffectDelayIndex;
constexpr int MixerVisibleEffectCount =
    OUTPUT_EFFECT_COUNT - MixerFirstVisibleEffectIndex;
constexpr int MixerEffectRowEnd = MixerFirstEffectRow + MixerVisibleEffectCount;

int GetMixerStripX(int strip) { return MixerStripX + strip * MixerStripStride; }

int32_t ScaleVuBarsForHeight(int32_t bars, int meterHeight) {
  const int32_t localMeterMax = meterHeight * 10 - 1;
  return std::min<int32_t>((bars * localMeterMax) / VU_METER_MAX,
                           localMeterMax);
}

FourCC GetChannelVolumeFourCC(int channel) {
  static const FourCC ids[] = {
      FourCC::VarChannel1Volume, FourCC::VarChannel2Volume,
      FourCC::VarChannel3Volume, FourCC::VarChannel4Volume,
      FourCC::VarChannel5Volume, FourCC::VarChannel6Volume,
      FourCC::VarChannel7Volume, FourCC::VarChannel8Volume};
  return ids[channel];
}

FourCC GetChannelSendFourCC(int channel, int fx) {
  switch (fx) {
  case kOutputEffectSlot1Index: {
    static const FourCC ids[] = {
        FourCC::VarChannel1FX1Send, FourCC::VarChannel2FX1Send,
        FourCC::VarChannel3FX1Send, FourCC::VarChannel4FX1Send,
        FourCC::VarChannel5FX1Send, FourCC::VarChannel6FX1Send,
        FourCC::VarChannel7FX1Send, FourCC::VarChannel8FX1Send};
    return ids[channel];
  }
  case kOutputEffectSlot2Index: {
    static const FourCC ids[] = {
        FourCC::VarChannel1FX2Send, FourCC::VarChannel2FX2Send,
        FourCC::VarChannel3FX2Send, FourCC::VarChannel4FX2Send,
        FourCC::VarChannel5FX2Send, FourCC::VarChannel6FX2Send,
        FourCC::VarChannel7FX2Send, FourCC::VarChannel8FX2Send};
    return ids[channel];
  }
  case kOutputEffectSlot3Index: {
    static const FourCC ids[] = {
        FourCC::VarChannel1FX3Send, FourCC::VarChannel2FX3Send,
        FourCC::VarChannel3FX3Send, FourCC::VarChannel4FX3Send,
        FourCC::VarChannel5FX3Send, FourCC::VarChannel6FX3Send,
        FourCC::VarChannel7FX3Send, FourCC::VarChannel8FX3Send};
    return ids[channel];
  }
  case kOutputEffectSlot4Index: {
    static const FourCC ids[] = {
        FourCC::VarChannel1FX4Send, FourCC::VarChannel2FX4Send,
        FourCC::VarChannel3FX4Send, FourCC::VarChannel4FX4Send,
        FourCC::VarChannel5FX4Send, FourCC::VarChannel6FX4Send,
        FourCC::VarChannel7FX4Send, FourCC::VarChannel8FX4Send};
    return ids[channel];
  }
  default:
    NAssert(false);
    return FourCC::VarChannel1FX1Send;
  }
}

int GetEffectIndexForMixerRow(int row) {
  if (row >= MixerFirstEffectRow && row < MixerEffectRowEnd) {
    return MixerFirstVisibleEffectIndex + row - MixerFirstEffectRow;
  }

  NAssert(false);
  return kOutputEffectSlot1Index;
}

void CopyShortName(const char *source, char *destination) {
  NAssert(destination != nullptr);

  for (int i = 0; i < 3; ++i) {
    destination[i] = (source != nullptr && source[i] != '\0') ? source[i] : ' ';
  }
  destination[3] = '\0';
}

} // namespace

MixerView::MixerView(GUIWindow &w, ViewData *viewData)
    : FieldView(w, viewData), selectedStrip_(0), selectedRow_(0) {
  static_assert(MixerLineInRow == MixerRowCount - 1,
                "the line input row must stay the last mixer row");
  for (int strip = 0; strip < MixerStripCount; ++strip) {
    for (int row = 0; row < MixerRowCount; ++row) {
      fieldGrid_[strip][row] = nullptr;
    }
  }
  initMixerFields();
}

MixerView::~MixerView() {}

void MixerView::Reset() {
  ClearFocus();
  fieldList_.clear();
  channelVolumeFields_.clear();
  channelSendFields_.clear();
  fxReturnFields_.clear();
  masterVolumeField_.clear();
  lineInMixField_.clear();

  for (int strip = 0; strip < MixerStripCount; ++strip) {
    for (int row = 0; row < MixerRowCount; ++row) {
      fieldGrid_[strip][row] = nullptr;
    }
  }
  selectedStrip_ = 0;
  selectedRow_ = 0;
  initMixerFields();
}

void MixerView::OnFocus() {
  setTitle(etl::string<23>("Mixer"));

  // SetFocus keeps the Song cursor in sync while navigating the Mixer. Restore
  // that shared cursor here to keep the opposite direction in sync as well.
  selectedStrip_ = std::clamp(viewData_->songX_, 0, MixerStripCount - 1);

  if (viewData_ && selectedRow_ >= MixerFirstEffectRow &&
      selectedRow_ < MixerEffectRowEnd) {
    const int effectIndex =
        std::clamp(ClampOutputEffectSlotIndex(viewData_->currentFXSlot_),
                   kOutputEffectDelayIndex, kOutputEffectReverbIndex);
    selectedRow_ =
        MixerFirstEffectRow + effectIndex - MixerFirstVisibleEffectIndex;
  }
  syncFocusToSelection();
}

void MixerView::SetFocus(UIField *field) {
  FieldView::SetFocus(field);

  if (!field) {
    return;
  }

  for (int strip = 0; strip < MixerStripCount; ++strip) {
    for (int row = 0; row < MixerRowCount; ++row) {
      if (fieldGrid_[strip][row] == field) {
        selectedStrip_ = strip;
        selectedRow_ = row;
        if (strip < SONG_CHANNEL_COUNT) {
          viewData_->songX_ = strip;
        }
        return;
      }
    }
  }
}

UIField *MixerView::GetSelectedField() {
  UIField *field = fieldGrid_[selectedStrip_][selectedRow_];
  if (!field) {
    field = fieldGrid_[selectedStrip_][0];
  }
  if (!field && !fieldList_.empty()) {
    field = *fieldList_.begin();
  }
  return field;
}

void MixerView::syncFocusToSelection() {
  UIField *field = GetSelectedField();
  if (field) {
    SetFocus(field);
  }
}

void MixerView::updateCursor(int dx, int dy) {
  selectedStrip_ += dx;
  selectedRow_ += dy;
  if (selectedRow_ < 0) {
    selectedRow_ = 0;
  }
  if (selectedRow_ >= MixerRowCount) {
    selectedRow_ = MixerRowCount - 1;
  }

  if (selectedStrip_ < 0) {
    selectedStrip_ = 0;
  }
  if (selectedStrip_ >= MixerStripCount) {
    selectedStrip_ = MixerStripCount - 1;
  }

  // The line input has no strip of its own, so its row stays under channel 01.
  if (selectedRow_ == MixerLineInRow) {
    selectedStrip_ = 0;
  }

  syncFocusToSelection();
  isDirty_ = true;
}

void MixerView::switchSoloMode() {
  if (selectedStrip_ < 0 || selectedStrip_ >= SONG_CHANNEL_COUNT) {
    return;
  }

  UIController *controller = UIController::GetInstance();
  controller->SwitchSoloMode(selectedStrip_, selectedStrip_,
                             (viewMode_ == VM_NORMAL));
  viewMode_ = (viewMode_ != VM_SOLOON) ? VM_SOLOON : VM_NORMAL;
  isDirty_ = true;
};

void MixerView::unMuteAll() {
  UIController *controller = UIController::GetInstance();
  controller->UnMuteAll();
  isDirty_ = true;
};

void MixerView::toggleMute() {
  if (selectedStrip_ < 0 || selectedStrip_ >= SONG_CHANNEL_COUNT) {
    return;
  }

  UIController *controller = UIController::GetInstance();
  controller->ToggleMute(selectedStrip_, selectedStrip_);
  viewMode_ = (viewMode_ != VM_MUTEON) ? VM_MUTEON : VM_NORMAL;
  isDirty_ = true;
};

void MixerView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed) {
    if (viewMode_ == VM_MUTEON && (mask & EPBM_NAV)) {
      toggleMute();
    }
    if (viewMode_ == VM_SOLOON && (mask & EPBM_NAV)) {
      switchSoloMode();
    }
    SetDirty(true);
    return;
  }

  if (mask == EPBM_PLAY) {
    togglePlay();
    return;
  }

  if ((mask & EPBM_NAV) && (mask & EPBM_UP)) {
    ViewType vt = VT_SONG;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }

  if ((mask & EPBM_NAV) && (mask & EPBM_DOWN)) {
    if (viewData_ && selectedRow_ >= MixerFirstEffectRow &&
        selectedRow_ < MixerEffectRowEnd) {
      viewData_->currentFXSlot_ = GetEffectIndexForMixerRow(selectedRow_);
    }
    ViewType vt = VT_MASTER_EFFECTS;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }

  if ((mask & EPBM_NAV) && (mask & EPBM_ALT)) {
    unMuteAll();
    return;
  }

  if ((mask & EPBM_EDIT) && (mask & EPBM_NAV)) {
    toggleMute();
    return;
  }

  if ((mask & EPBM_ENTER) && (mask & EPBM_NAV)) {
    switchSoloMode();
    return;
  }

  if ((mask & EPBM_EDIT) && (mask & EPBM_PLAY)) {
    if (!Player::GetInstance()->IsRunning()) {
      switchToRecordView();
    }
    return;
  }

  if (mask & (EPBM_ENTER | EPBM_EDIT)) {
    FieldView::ProcessButtonMask(mask, pressed);
    SetDirty(true);
    return;
  }

  // Keep the solo state while NAV is pressed ahead of ENTER. Otherwise the
  // NAV-down event clears it and NAV+ENTER starts a new solo instead of
  // restoring the snapshot.
  if (viewMode_ == VM_SOLOON && mask == EPBM_NAV) {
    SetDirty(true);
    return;
  }

  viewMode_ = VM_NORMAL;
  SetDirty(true);
  processNormalButtonMask(mask);
}

void MixerView::processNormalButtonMask(unsigned int mask) {
  if (mask & EPBM_LEFT) {
    updateCursor(-1, 0);
  }
  if (mask & EPBM_RIGHT) {
    updateCursor(1, 0);
  }
  if (mask & EPBM_UP) {
    updateCursor(0, -1);
  }
  if (mask & EPBM_DOWN) {
    updateCursor(0, 1);
  }
}

void MixerView::processSelectionButtonMask(unsigned int mask) { (void)mask; }

void MixerView::initMixerFields() {
  Project *project = viewData_ ? viewData_->project_ : nullptr;
  if (!project) {
    return;
  }

  channelVolumeFields_.clear();
  channelSendFields_.clear();
  fxReturnFields_.clear();
  masterVolumeField_.clear();
  lineInMixField_.clear();

  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    GUIPoint volumePos(GetMixerStripX(channel), MixerFieldBaseY);
    Variable *volume = project->FindVariable(GetChannelVolumeFourCC(channel));
    if (volume) {
      channelVolumeFields_.emplace_back(volumePos, *volume, "%2.2X", 0,
                                        Project::MixerVolumeMaximum, 1, 0x10);
      UIField *field = &(*channelVolumeFields_.rbegin());
      fieldGrid_[channel][0] = field;
      fieldList_.insert(fieldList_.end(), field);
    }

    // the columns for the fx to master send levels
    for (int row = MixerFirstEffectRow; row < MixerEffectRowEnd; ++row) {
      int effectIndex = GetEffectIndexForMixerRow(row);
      GUIPoint sendPos(GetMixerStripX(channel), MixerFieldBaseY + row);
      Variable *send =
          project->FindVariable(GetChannelSendFourCC(channel, effectIndex));
      if (send) {
        channelSendFields_.emplace_back(sendPos, *send, "%2.2X", 0,
                                        OutputEffectSendMaximum, 1, 0x10);
        UIField *field = &(*channelSendFields_.rbegin());
        fieldGrid_[channel][row] = field;
        fieldList_.insert(fieldList_.end(), field);
      }
    }
  }

  // the column for the fx to master return level
  for (int row = MixerFirstEffectRow; row < MixerEffectRowEnd; ++row) {
    int effectIndex = GetEffectIndexForMixerRow(row);
    GUIPoint returnPos(GetMixerStripX(MixerMasterStripIndex),
                       MixerFieldBaseY + row);
    Variable *returnVolume =
        project->FindVariable(GetOutputEffectReturnVariableId(effectIndex));
    if (returnVolume) {
      fxReturnFields_.emplace_back(returnPos, *returnVolume, "%2.2X", 0,
                                   OutputEffectReturnMaximum, 1, 0x10);
      UIField *field = &(*fxReturnFields_.rbegin());
      fieldGrid_[MixerMasterStripIndex][row] = field;
      fieldList_.insert(fieldList_.end(), field);
    }
  }

  GUIPoint masterPos(GetMixerStripX(MixerMasterStripIndex),
                     MixerFieldBaseY + MixerVolumeRow);
  Variable *masterVolume = project->FindVariable(FourCC::VarMasterVolume);
  if (masterVolume) {
    masterVolumeField_.emplace_back(masterPos, *masterVolume, "%2.2X", 0,
                                    Project::MixerVolumeMaximum, 1, 0x10);
    UIField *field = &(*masterVolumeField_.begin());
    fieldGrid_[MixerMasterStripIndex][MixerVolumeRow] = field;
    fieldList_.insert(fieldList_.end(), field);
  }

  // The line input level. It is mixed into the master output after the master
  // bus, so unlike the channels it has no delay or reverb send of its own. It
  // reads out under its meter rather than on the row the sends end at.
  GUIPoint lineInPos(GetMixerStripX(0), MixerLineInValueY);
  Variable *lineInLevel = project->FindVariable(FourCC::VarLineInMixLevel);
  if (lineInLevel) {
    lineInMixField_.emplace_back(lineInPos, *lineInLevel, "%2.2X", 0,
                                 Project::MixerVolumeMaximum, 1, 0x10);
    UIField *field = &(*lineInMixField_.begin());
    fieldGrid_[0][MixerLineInRow] = field;
    fieldList_.insert(fieldList_.end(), field);
  }

  syncFocusToSelection();
}

void MixerView::drawMixerLabels(Player *player, GUITextProperties props) {
  for (int row = 0; row < MixerRowCount; ++row) {
    char rowLabel[4];
    if (row == MixerVolumeRow) {
      CopyShortName("VOL", rowLabel);
    } else if (row == MixerLineInRow) {
      CopyShortName("LIN", rowLabel);
    } else {
      CopyShortName(
          GetOutputEffectShortNameForSlot(GetEffectIndexForMixerRow(row)),
          rowLabel);
    }

    if (row == selectedRow_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    }
    // The line input label follows its value under the meter, so that the two
    // still read together.
    const int labelY =
        row == MixerLineInRow ? MixerLineInValueY : MixerFieldBaseY + row;
    DrawRegionString(TEXT_REGION_BODY, MixerRowLabelX, labelY, rowLabel, props);
  }

  for (int strip = 0; strip < MixerStripCount; ++strip) {
    char label[3] = {' ', ' ', '\0'};
    if (strip < SONG_CHANNEL_COUNT) {
      if (player->IsChannelMuted(strip)) {
        label[0] = 'M';
        label[1] = static_cast<char>('1' + strip);
      } else {
        label[0] = '0';
        label[1] = static_cast<char>('1' + strip);
      }
    } else {
      label[0] = 'M';
      label[1] = 'B';
    }

    if (strip == selectedStrip_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    }

    DrawRegionString(TEXT_REGION_BODY, GetMixerStripX(strip), MixerStripLabelY,
                     label, props);
  }

  SetColor(CD_NORMAL);
  props.invert_ = false;
}

void MixerView::DrawView() {
  Clear();

  GUITextProperties props;

  Player *player = Player::GetInstance();

  props.invert_ = false;
  drawMixerLabels(player, props);
  FieldView::Redraw();
  drawMasterVuMeter(player, props, true, MixerMasterVuXOffset,
                    MixerVuMeterHeight, false);
  drawLineInVuMeter(props, true);
  drawMap();
  drawNotes();

  if (player->IsRunning()) {
    OnPlayerUpdate(PET_UPDATE);
  }
}

void MixerView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {
  ScreenView::OnPlayerUpdate(eventType, tick);
}

void MixerView::AnimationUpdate() {
  if ((AppWindow::GetAnimationFrameCounter() % PICO_CLOCK_HZ) == 0) {
    SetDirty(true);
  }
  ScreenView::AnimationUpdate();
  GUITextProperties props;

  Player *player = Player::GetInstance();
  if (!viewData_ || !player) {
    w_.Flush();
    return;
  }

  etl::array<stereosample, SONG_CHANNEL_COUNT> *levels =
      player->GetMixerLevels();
  if (levels) {
    drawChannelVUMeters(levels, player, props);
    drawMasterVuMeter(player, props, false, MixerMasterVuXOffset,
                      MixerVuMeterHeight, false);
  }

  drawLineInVuMeter(props, false);

  drawNotes();

  w_.Flush();
}

void MixerView::drawChannelVUMeters(
    etl::array<stereosample, SONG_CHANNEL_COUNT> *levels, Player *player,
    GUITextProperties props, bool forceRedraw) {
  constexpr int displayChannelCount = SONG_CHANNEL_COUNT;
  if (!forceRedraw) {
    bool anyChanges = false;
    for (int i = 0; i < displayChannelCount; i++) {
      int32_t leftBars = 0;
      int32_t rightBars = 0;
      if (!player->IsChannelMuted(i)) {
        amplitudeToBars(levels->at(i), &leftBars, &rightBars);
        leftBars = ScaleVuBarsForHeight(leftBars, MixerVuMeterHeight);
        rightBars = ScaleVuBarsForHeight(rightBars, MixerVuMeterHeight);
      }
      if (leftBars != prevLeftVU_[i + 1] || rightBars != prevRightVU_[i + 1]) {
        anyChanges = true;
        break;
      }
    }
    if (!anyChanges) {
      return;
    }
  }

  GUIPoint pos(MixerStripX, GetAnchor()._y + MixerVuMeterHeight - 1);
  for (int i = 0; i < displayChannelCount; i++) {
    int32_t leftBars = 0;
    int32_t rightBars = 0;
    if (!player->IsChannelMuted(i)) {
      amplitudeToBars(levels->at(i), &leftBars, &rightBars);
    }

    drawVUMeter(leftBars, rightBars, pos, props, i + 1, forceRedraw,
                MixerVuMeterHeight);
    pos._x += MixerStripStride;
  }
}

void MixerView::drawLineInVuMeter(GUITextProperties props, bool forceRedraw) {
  // Use body-local rows, as the editable level field does. The meter's
  // baseline is one row above that field.
  int32_t leftBars = 0;
  int32_t rightBars = 0;
  amplitudeToBars(MixerService::GetInstance()->GetLineInLevels(), &leftBars,
                  &rightBars);

  GUIPoint pos(GetMixerStripX(0), MixerLineInVuBaseY);
  drawVUMeter(leftBars, rightBars, pos, props, VU_LINE_IN_INDEX, forceRedraw,
              MixerLineInVuRows);
}

void MixerView::togglePlay() {
  if (!viewData_) {
    return;
  }

  Player *player = Player::GetInstance();
  int startChannel = selectedStrip_;
  if (startChannel >= SONG_CHANNEL_COUNT) {
    startChannel = viewData_->songX_;
  }
  if (startChannel < 0 || startChannel >= SONG_CHANNEL_COUNT) {
    startChannel = 0;
  }

  player->OnStartButton(PM_CHAIN, static_cast<unsigned int>(startChannel), true,
                        viewData_->chainRow_);
}
