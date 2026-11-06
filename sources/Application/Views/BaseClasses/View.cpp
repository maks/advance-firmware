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

#include "View.h"
#include "Application/AppWindow.h"
#include "Application/Player/PlayButtonBehavior.h"
#include "Application/Player/Player.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Utils/mathutils.h"
#include "Application/Views/BaseClasses/PlaybackStatusIndicator.h"
#include "Application/Views/RecordView.h"
#include "Application/Views/SampleEditorView.h"
#include "Application/Views/VoiceScreenUtils.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "ModalView.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <UIFramework/SimpleBaseClasses/EventManager.h>
#include <nanoprintf.h>

bool View::initPrivate_ = false;

int View::margin_ = 0;
int View::songRowCount_ = 16;

namespace {
BatteryState batteryDisplayState = {
    .percentage = 0,
    .voltage_mv = 0,
    .temperature_c = 0,
    .charging = false,
    .error = false,
};

uint32_t lastBatteryPollFrame = 0;
bool batteryDisplayInitialized = false;

bool currentInstrumentSupportsPreview(ViewData *viewData) {
  if (!viewData || !viewData->project_) {
    return false;
  }

  InstrumentBank *bank = viewData->project_->GetInstrumentBank();
  if (!bank) {
    return false;
  }

  I_Instrument *instrument =
      bank->GetInstrument(viewData->currentInstrumentID_);
  return SupportsInstrumentPreview(instrument);
}

} // namespace

View::View(GUIWindow &w, ViewData *viewData)
    : w_(w), viewData_(viewData), needsRedraw_(false), isVisible_(true),
      vuMeterCount_(0), viewMode_(VM_NORMAL), isDirty_(true),
      viewType_(VT_SONG), hasFocus_(false), playButtonPressed_(false),
      powerButtonPressed_(false), powerButtonHoldCount_(0),
      deferredPlayButtonActive_(false), deferredPlayButtonKillTriggered_(false),
      deferredPlayButtonMask_(0), deferredPlayButtonPressedAt_(0) {
  if (!initPrivate_) {
    View::margin_ = 0;
    songRowCount_ = 16;
    initPrivate_ = true;
  }
  mask_ = 0;
  locked_ = false;
  modalView_ = 0;
  modalViewCallback_ = ModalViewCallback();
  // Initialize VU meter tracking variables
  for (int i = 0; i < VU_METER_COUNT; i++) {
    prevLeftVU_[i] = 0;
    prevRightVU_[i] = 0;
  }
}
GUIPoint View::GetAnchor() {
  // Original code had a dynamic anchor point dending on song count, but
  // changing the song count didn't work anyway given that there are many places
  // where it was statically defined as 8. Other screens also don't fit with a
  // dynamic anchor point. Coordinates are local to the body text region.
  return GUIPoint(5, 2);
}

GUIPoint View::GetTitlePosition() { return GUIPoint(0, 0); };

void View::setTitle(const etl::istring &title) { title_ = title; }

void View::drawTitle(uint8_t region) {
  GUIPoint pos = GetTitlePosition();
  GUITextProperties props;

  SetColor(CD_NORMAL);
  DrawRegionString(region, pos._x, pos._y, title_.c_str(), props);
}

bool View::Lock() {
  if (locked_)
    return false;
  locked_ = true;
  return true;
};

void View::WaitForObject() {
  while (locked_) {
  };
}

void View::Unlock() { locked_ = false; }

void View::drawMap() {
  // Region-local cells: FONTSMALL is 16x24, so five rows occupy 120px.
  GUIPoint pos(0, 0);
  GUITextProperties props;
  I_Instrument *currentInstrument = nullptr;
  if (viewData_ && viewData_->project_) {
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    if (bank) {
      currentInstrument = bank->GetInstrument(viewData_->currentInstrumentID_);
    }
  }
  const bool showVoiceOnMap =
      ShouldShowAudioVoiceScreenOnMap(viewType_, currentInstrument);

  // draw entire map
  SetColor(CD_NORMAL);
  char buffer[6];
  props.invert_ = false;
  // row1
  strcpy(buffer, "D    ");
  DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, buffer, props);
  pos._y++;
  // row2
  strcpy(buffer, "P G  ");
  DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, buffer, props);
  pos._y++;
  // row3
  strcpy(buffer, showVoiceOnMap ? "SCPIV" : "SCPI ");
  DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, buffer, props);
  pos._y++;
  // row4
  strcpy(buffer, "M TT ");
  DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, buffer, props);
  pos._y++;
  // row5
  strcpy(buffer, "X    ");
  DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, buffer, props);

  // draw current screen on map
  SetColor(CD_HILITE2);
  pos._y = 0;
  switch (viewType_) {
  case VT_CHAIN:
    pos._x += 1;
    pos._y += 2;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "C", props);
    break;
  case VT_PHRASE:
    pos._x += 2;
    pos._y += 2;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "P", props);
    break;
  case VT_DEVICE:
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "D", props);
    break;
  case VT_PROJECT:
    pos._y += 1;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "P", props);
    break;
  case VT_VOICE:
    if (showVoiceOnMap) {
      pos._x += 4;
      pos._y += 2;
      DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "V", props);
    }
    break;
  case VT_INSTRUMENT:
    pos._x += 3;
    pos._y += 2;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "I", props);
    break;
  case VT_TABLE: // under phrase
    pos._x += 2;
    pos._y += 3;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "T", props);
    break;
  case VT_TABLE2: // under instrument
    pos._x += 3;
    pos._y += 3;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "T", props);
    break;
  case VT_GROOVE:
    pos._x += 2;
    pos._y += 1;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "G", props);
    break;
  case VT_MIXER:
    pos._y += 3;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "M", props);
    break;
  case VT_MASTER_EFFECTS:
    pos._y += 4;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "X", props);
    break;
  default: // VT_SONG
    pos._y += 2;
    DrawRegionString(TEXT_REGION_MAP, pos._x, pos._y, "S", props);
  }
}

void View::drawNotes() {
  int initialX = View::margin_ + 1;
  int initialY = 1;
  GUIPoint pos(initialX, initialY);
  GUITextProperties props;

  Player *player = Player::GetInstance();
  const bool isSequencerPlayback =
      player->IsRunning() && viewData_->playMode_ != PM_AUDITION;

  props.invert_ = true;
  const int channelCount = SONG_CHANNEL_COUNT;
  for (int i = 0; i < channelCount; i++) {
    if (i == viewData_->songX_) {
      SetColor(CD_HILITE2);
    } else {
      SetColor(CD_HILITE1);
    }
    const bool showPlaybackVisualizer = ShowsChannelPlaybackVisualizer(
        isSequencerPlayback, viewData_->song_->IsMidiInputChannelAssigned(i),
        player->IsMidiInputChannelPlaying(static_cast<uint8_t>(i)));
    if (showPlaybackVisualizer) {
      uint8_t sliceIndex = 0;
      if (player->GetPlayedSliceIndex(i, sliceIndex)) {
        DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, "SL", props);
        pos._y++;
        char sliceBuffer[3];
        sliceBuffer[0] = static_cast<char>('0' + (sliceIndex / 10));
        sliceBuffer[1] = static_cast<char>('0' + (sliceIndex % 10));
        sliceBuffer[2] = '\0';
        DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, sliceBuffer,
                         props);
        pos._y++;
        DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y,
                         player->GetPlayedInstrument(i),
                         props); // draw instrument number
      } else {
        DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y,
                         player->GetPlayedNote(i),
                         props); // row for the note values
        pos._y++;
        DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y,
                         player->GetPlayedOctive(i),
                         props); // row for the octive values
        pos._y++;
        DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y,
                         player->GetPlayedInstrument(i),
                         props); // draw instrument number
      }
    } else {
      DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, "  ",
                       props); // row for the note
                               // values
      pos._y++;
      DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, "  ",
                       props); // row for the octive values
      pos._y++;
      DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, "  ",
                       props); // draw instrument number
    }
    pos._y = initialY;
    pos._x += 3;
  }
}

void View::drawMasterVuMeter(Player *player, GUITextProperties props,
                             bool forceRedraw, uint8_t xoffset,
                             uint8_t meterHeight, bool useMeterRegion) {
  stereosample playerLevel = player->GetMasterLevel();

  // Convert amplitude to bar levels
  int32_t leftBars, rightBars;
  amplitudeToBars(playerLevel, &leftBars, &rightBars);

  // we start at the bottom of the VU meter and draw it growing upwards
  GUIPoint pos = GetAnchor();
  pos._x += xoffset;
  pos._y += meterHeight - 1; // -1 to align with song grid

  // Map screens reserve a dedicated two-channel region. Other layouts, such
  // as Record, retain the normal-font meter in their body region. Mixer also
  // explicitly keeps its master meter alongside the channel strips.
  uint8_t region = TEXT_REGION_BODY;
  const TextRegionInfo *vu =
      static_cast<AppWindow &>(w_).GetTextRegion(TEXT_REGION_VU);
  if (useMeterRegion && vu && vu->definition.font == TEXT_FONT_TINY) {
    region = TEXT_REGION_VU;
    meterHeight = vu->rows;
    pos = GUIPoint(0, meterHeight - 1);
  }
  // Use index 0 for the master VU meter
  drawVUMeter(leftBars, rightBars, pos, props, 0, forceRedraw, meterHeight,
              region);
}

void View::drawVUMeter(int32_t leftBars, int32_t rightBars, GUIPoint pos,
                       GUITextProperties props, int vuIndex, bool forceRedraw,
                       uint8_t meterHeight, uint8_t region) {
  // Half-height glyphs double the bar units while preserving physical motion
  // and the height of the clipping band.
  const int stepScale = region == TEXT_REGION_VU ? 2 : 1;
  const int32_t localMeterMax = meterHeight * 10 - stepScale;
  const int32_t coloredRows = std::max<int32_t>(
      2, (7 * meterHeight + (VU_METER_HEIGHT / 2)) / VU_METER_HEIGHT);
  const int32_t warnLevel = meterHeight - coloredRows - 1;
  const int32_t clipLevel = meterHeight - stepScale;

  leftBars = (leftBars * localMeterMax) / VU_METER_MAX;
  rightBars = (rightBars * localMeterMax) / VU_METER_MAX;

  // Clamp the values to the maximum height
  leftBars = std::min<int32_t>(leftBars, localMeterMax);
  rightBars = std::min<int32_t>(rightBars, localMeterMax);

  // Add inertia effect by limiting the rate of change
  // Maximum step change allowed per update
  // Half-height glyphs need twice as many bar steps for the same physical
  // movement. Keep the existing response of ordinary mixer channel meters.
  const int maxStepChange = 20 * stepScale;
  const int fallStepChange = 10 * stepScale;

  // For rising levels (current > previous), allow faster response
  if (leftBars > prevLeftVU_[vuIndex]) {
    // If the difference is greater than maxStepChange, limit it
    if (leftBars - prevLeftVU_[vuIndex] > maxStepChange) {
      leftBars = prevLeftVU_[vuIndex] + maxStepChange;
    }
  }
  // For falling levels (current < previous), add more inertia for a slower fall
  else if (leftBars < prevLeftVU_[vuIndex]) {
    // Use a smaller step for falling levels to create more inertia
    if (prevLeftVU_[vuIndex] - leftBars > fallStepChange) {
      leftBars = prevLeftVU_[vuIndex] - fallStepChange;
    }
  }

  // Same for right channel
  if (rightBars > prevRightVU_[vuIndex]) {
    if (rightBars - prevRightVU_[vuIndex] > maxStepChange) {
      rightBars = prevRightVU_[vuIndex] + maxStepChange;
    }
  } else if (rightBars < prevRightVU_[vuIndex]) {
    if (prevRightVU_[vuIndex] - rightBars > fallStepChange) {
      rightBars = prevRightVU_[vuIndex] - fallStepChange;
    }
  }

  // Left channel: Handle level changes
  bool leftChanged = (leftBars != prevLeftVU_[vuIndex]);
  bool rightChanged = (rightBars != prevRightVU_[vuIndex]);

  if (forceRedraw || leftChanged || rightChanged) {
    // If forcing redraw or level changed, redraw the entire meter

    // Then draw the active cells with inversion enabled
    props.invert_ = false;

    for (int i = 0; i < meterHeight; i++) {
      // Set appropriate color based on level
      if (i >= clipLevel) {
        SetColor(CD_ERROR);
      } else if (i > warnLevel) {
        SetColor(CD_WARN);
      } else {
        SetColor(CD_INFO);
      }

      // draw left channel if changed
      if (forceRedraw || leftChanged) {
        DrawRegionString(region, pos._x, pos._y - i,
                         char_bargraph_s(leftBars - 10 * i), props);
      }

      // draw right channel if changed
      if (forceRedraw || rightChanged) {
        DrawRegionString(region, pos._x + 1, pos._y - i,
                         char_bargraph_s(rightBars - 10 * i), props);
      }
    }
  }

  // Store the current values for next time
  prevLeftVU_[vuIndex] = leftBars;
  prevRightVU_[vuIndex] = rightBars;
}

void View::DoModal(ModalView *view, ModalViewCallback cb) {
  if (modalView_)
    static_cast<AppWindow &>(w_).CloseModalOverlay();
  modalView_ = view;
  modalView_->OnFocus();
  modalViewCallback_ = cb;
  // Opening a popup does not invalidate the underlying screen.
  modalView_->SetDirty(true);
};

void View::DismissModal() {
  if (modalView_ && modalView_->IsFinished()) {
    ModalView *finishedModal = modalView_;
    const uint32_t finishedModalId = finishedModal->GetInstanceId();
    ModalViewCallback callback = modalViewCallback_;
    static_cast<AppWindow &>(w_).CloseOverlay(finishedModalId);

    // Clear current modal first so callback can safely open another modal.
    modalView_ = nullptr;
    modalViewCallback_ = ModalViewCallback();

    if (callback) {
      callback(*this, *finishedModal);
    }

    // Callback may have already replaced this instance in shared storage.
    if (finishedModal->GetInstanceId() == finishedModalId) {
      finishedModal->Destroy();
    }

    isDirty_ = true;
  }
};

void View::Redraw() {
  // Modal rendering happens after AnimationUpdate completes the base frame.
  // Capturing here would flush a title cleared by DrawView but not rebuilt yet.
  DrawView();
  isDirty_ = false;
};

void View::SetDirty(bool isDirty) { isDirty_ = isDirty; };

void View::ProcessButton(unsigned short mask, unsigned short heldMask,
                         bool pressed) {
  const uint16_t eventMask = static_cast<uint16_t>(mask);
  const uint16_t physicalHeldMask = static_cast<uint16_t>(heldMask);
  mask_ = physicalHeldMask;
  const bool wasPlayPressed = playButtonPressed_;
  const bool isPlayPressed = (physicalHeldMask & EPBM_PLAY) != 0;
  const bool playReleasedEvent = !pressed && wasPlayPressed && !isPlayPressed;
  playButtonPressed_ = isPlayPressed;

  if (!pressed) {
    powerButtonPressed_ = false;
  } else if (physicalHeldMask & EPBM_POWER) {
    powerButtonPressed_ = pressed;
  }

  if (handleDeferredPlayButton(eventMask, physicalHeldMask, pressed,
                               playReleasedEvent)) {
    if (isDirty_) {
      ((AppWindow &)w_).SetDirty();
    }
    return;
  }

  // Normal button processing
  if (modalView_) {
    modalView_->ProcessButton(mask, heldMask, pressed);
    // checks if modal is done and if it is disposes of it:
    DismissModal();
  } else {
    ProcessButtonMask(mask, pressed);
  }

  if (isDirty_)
    ((AppWindow &)w_).SetDirty();
};

bool View::shouldDeferPlayButton(uint16_t mask) const {
  if (!ShouldDeferPlayButtonInput() || (mask & EPBM_PLAY) == 0 ||
      modalView_ != nullptr || (mask & EPBM_ENTER)) {
    return false;
  }

  Player *player = Player::GetInstance();
  if ((viewType_ == VT_INSTRUMENT || viewType_ == VT_VOICE) &&
      ShouldBypassPlayButtonDeferralForPreview(
          currentInstrumentSupportsPreview(viewData_), mask, EPBM_PLAY,
          EPBM_ALT)) {
    return false;
  }

  if (mask == static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY)) {
    return true;
  }

  switch (viewType_) {
  case VT_SONG:
    if (mask == EPBM_PLAY || mask == (EPBM_NAV | EPBM_PLAY) ||
        mask == (EPBM_ALT | EPBM_PLAY)) {
      return true;
    }
    return mask == (EPBM_EDIT | EPBM_PLAY) && player != nullptr &&
           player->GetSequencerMode() == SM_LIVE;

  case VT_CHAIN:
  case VT_PHRASE:
  case VT_TABLE:
  case VT_TABLE2:
  case VT_GROOVE:
  case VT_INSTRUMENT:
  case VT_VOICE:
    return mask == EPBM_PLAY || mask == (EPBM_NAV | EPBM_PLAY);

  case VT_PROJECT:
  case VT_DEVICE:
  case VT_THEME:
  case VT_MIXER:
  case VT_MASTER_EFFECTS:
    return mask == EPBM_PLAY;

  default:
    return false;
  }
}

bool View::handleDeferredPlayButton(uint16_t eventMask, uint16_t heldMask,
                                    bool pressed, bool playReleasedEvent) {
  if (deferredPlayButtonActive_) {
    if (!playReleasedEvent) {
      if ((heldMask & EPBM_PLAY) == 0) {
        deferredPlayButtonActive_ = false;
        deferredPlayButtonKillTriggered_ = false;
        deferredPlayButtonMask_ = 0;
        deferredPlayButtonPressedAt_ = 0;
        return false;
      }

      if ((eventMask & EPBM_PLAY) != 0) {
        if (shouldDeferPlayButton(eventMask)) {
          if (deferredPlayButtonMask_ !=
                  static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY) ||
              eventMask == static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY)) {
            deferredPlayButtonMask_ = eventMask;
          }
        }
        return true;
      }

      if (pressed &&
          (eventMask &
           (EPBM_ALT | EPBM_EDIT | EPBM_ENTER | EPBM_NAV | EPBM_SELECT)) != 0 &&
          shouldDeferPlayButton(heldMask)) {
        deferredPlayButtonMask_ = heldMask;
        return true;
      }

      deferredPlayButtonActive_ = false;
      deferredPlayButtonKillTriggered_ = false;
      deferredPlayButtonMask_ = 0;
      deferredPlayButtonPressedAt_ = 0;
      return false;
    }

    const uint16_t deferredMask = deferredPlayButtonMask_;
    const bool killTriggered = deferredPlayButtonKillTriggered_;
    deferredPlayButtonActive_ = false;
    deferredPlayButtonKillTriggered_ = false;
    deferredPlayButtonMask_ = 0;
    deferredPlayButtonPressedAt_ = 0;

    if (!killTriggered) {
      Player *player = Player::GetInstance();
      const bool isAltPlay =
          (deferredMask == static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY));
      // ALT+PLAY starts/stops the song from any screen while the sequencer
      // is in Song mode. On the song screen in Live mode it queues the
      // current row instead, so it needs to be handled by SongView itself.
      const bool startStopSong =
          isAltPlay &&
          (viewType_ != VT_SONG ||
           (player != nullptr && player->GetSequencerMode() == SM_SONG));
      if (startStopSong && player != nullptr && viewData_ != nullptr) {
        player->OnStartButton(PM_SONG, viewData_->songX_, false,
                              viewData_->songX_);
      } else {
        ProcessButtonMask(deferredMask, true);
      }
    }
    return true;
  }

  if (!pressed || !shouldDeferPlayButton(eventMask)) {
    return false;
  }

  System *system = System::GetInstance();
  deferredPlayButtonActive_ = true;
  deferredPlayButtonKillTriggered_ = false;
  deferredPlayButtonMask_ = eventMask;
  deferredPlayButtonPressedAt_ =
      (system != nullptr) ? static_cast<uint32_t>(system->GetClock()) : 0U;
  return true;
}

void View::Clear() { ((AppWindow &)w_).Clear(); }

void View::ForceClear() { ((AppWindow &)w_).Clear(true); }

void View::SetColor(ColorDefinition cd) { ((AppWindow &)w_).SetColor(cd); };

void View::DrawGraphicsRect(uint8_t region, const GUIRect &local,
                            ColorDefinition color) {
  static_cast<AppWindow &>(w_).DrawGraphicsRect(region, local, color);
}

void View::DrawRegionString(uint8_t region, int x, int y, const char *text,
                            const GUITextProperties &props) {
  static_cast<AppWindow &>(w_).DrawRegionString(region, text, GUIPoint(x, y),
                                                props);
}

void View::HighlightSelection(uint8_t region, const GUIRect &cells) {
  // Selection takes precedence over content colours without replacing glyphs.
  static_cast<AppWindow &>(w_).StyleRegionTextRect(region, cells, CD_HILITE2,
                                                   true);
}

void View::ClearRegionTextRect(uint8_t region, int x, int y, int width,
                               int height) {
  GUIRect rect(x, y, x + width, y + height);
  static_cast<AppWindow &>(w_).ClearRegionTextRect(region, rect);
}

void View::DrawRect(GUIRect &r, ColorDefinition color) {
  w_.SetCurrentRectColor(AppWindow::GetColor(color));
  w_.DrawRect(r);
}

void View::drawBattery(uint8_t region, uint8_t columns,
                       GUITextProperties &props) {
  // Five text cells: caps plus three glyphs containing six visible segments.
  constexpr int kBatteryWidgetWidth = 5;
  if (columns < kBatteryWidgetWidth)
    return;
  const uint32_t frameCounter = AppWindow::GetAnimationFrameCounter();
  constexpr uint32_t kBatteryPollIntervalFrames = PICO_CLOCK_HZ;

  if (!batteryDisplayInitialized ||
      (frameCounter - lastBatteryPollFrame) >= kBatteryPollIntervalFrames) {
    System::GetInstance()->GetBatteryState(batteryDisplayState);
    lastBatteryPollFrame = frameCounter;
    batteryDisplayInitialized = true;
  }

  GUIPoint battpos(0, 0);

  uint8_t batteryPercent = batteryDisplayState.percentage;
  if (batteryPercent > 100) {
    batteryPercent = 100;
  }

  ColorDefinition batteryColor = CD_NORMAL;
  if (batteryPercent <= 5) {
    batteryColor = CD_ERROR;
  } else if (batteryPercent < 20) {
    batteryColor = CD_WARN;
  } else if (batteryDisplayState.charging) {
    batteryColor = CD_INFO;
  }

  SetColor(batteryColor);

  GUITextProperties normalProps = props;
  normalProps.invert_ = false;

  int startX = columns - kBatteryWidgetWidth;
  ClearRegionTextRect(region, startX, battpos._y, kBatteryWidgetWidth, 1);

  char batteryText[kBatteryWidgetWidth + 1];
  batteryText[0] = char_battery_left_s[0];

  // Each interior glyph contains two segment slots. Empty, half, and full
  // states across six slots give the gauge seven distinct levels.
  const uint8_t filledHalfSegments =
      static_cast<uint8_t>((batteryPercent * 6U + 50U) / 100U);
  for (uint8_t segment = 0; segment < 3; ++segment) {
    const uint8_t segmentFill =
        filledHalfSegments > segment * 2U
            ? static_cast<uint8_t>(filledHalfSegments - segment * 2U)
            : 0U;
    const char *glyph = segmentFill >= 2U   ? char_battery_full_s
                        : segmentFill == 1U ? char_battery_half_s
                                            : char_battery_empty_s;
    batteryText[segment + 1U] = glyph[0];
  }

  batteryText[kBatteryWidgetWidth - 1] = batteryDisplayState.charging
                                             ? char_symbol_charging_s[0]
                                             : char_battery_right_s[0];
  batteryText[kBatteryWidgetWidth] = '\0';
  DrawRegionString(region, startX, battpos._y, batteryText, normalProps);
}

void View::drawTitlebar(uint8_t region, GUITextProperties &props) {
  const TextRegionInfo *info =
      static_cast<AppWindow &>(w_).GetTextRegion(region);
  if (!info)
    return;
  drawTitle(region);
  drawStatusIndicators(region, info->columns, props);
  drawBattery(region, info->columns, props);
}

void View::drawStatusIndicators(uint8_t region, uint8_t columns,
                                GUITextProperties &props) {
  // Seven status cells immediately before the five-cell battery widget:
  //   0: playing indicator (>)
  //   1: audition/MIDI/sample/phrase/chain/tail indicator (A/M/S/P/C/T)
  //   2: separator
  //   3-5: project BPM
  //   6: gap (space)
  if (columns < 12)
    return;
  const int kStatusWidgetStartX = columns - 12;
  GUIPoint statusPos(0, 0);

  GUITextProperties normalProps = props;
  normalProps.invert_ = false;

  // First two status cells each draw a space when inactive.
  Player *player = Player::GetInstance();
  const bool isRunning = player != nullptr && player->IsRunning();
  const bool isAuditioning = player != nullptr && player->IsAuditioning();
  const bool isMidiInputPlaying =
      player != nullptr && player->IsMidiInputPlaying();
  const bool isSamplePreviewing =
      player != nullptr && player->IsStreamingActive();
  const bool isTailPlaying =
      player != nullptr && !isSamplePreviewing && player->IsTailPlaying();
  const PlayMode playMode = viewData_ ? viewData_->playMode_ : PM_SONG;
  const PlaybackStatusIndicator playbackStatus = ResolvePlaybackStatusIndicator(
      isRunning, isAuditioning, isMidiInputPlaying, isSamplePreviewing,
      isTailPlaying, playMode);
  int indicatorX = kStatusWidgetStartX; // local column in the selected region

  // Playing indicator: ">" while any playback status is active.
  if (ShowsPlaybackIcon(playbackStatus)) {
    SetColor(CD_INFO);
    DrawRegionString(region, indicatorX, statusPos._y, char_playback_play_s,
                     normalProps);
  } else {
    DrawRegionString(region, indicatorX, statusPos._y, " ", normalProps);
  }
  indicatorX++;

  // Audition takes precedence over MIDI input and sample preview, which take
  // precedence over sequencer mode. Tail is only active once those states have
  // stopped.
  const char *statusSuffix = GetPlaybackStatusSuffix(playbackStatus);
  if (statusSuffix[0] != ' ') {
    SetColor(CD_INFO);
  }
  DrawRegionString(region, indicatorX, statusPos._y, statusSuffix, normalProps);
  indicatorX++;

  // Show the project tempo, including nudging, even while playback is stopped.
  char bpmText[4];
  const int tempo =
      viewData_ && viewData_->project_ ? viewData_->project_->GetTempo() : 0;
  npf_snprintf(bpmText, sizeof(bpmText), "%3d", tempo);
  SetColor(CD_WARN);
  DrawRegionString(region, indicatorX, statusPos._y, " ", normalProps);
  DrawRegionString(region, indicatorX + 1, statusPos._y, bpmText, normalProps);
  DrawRegionString(region, indicatorX + 4, statusPos._y, " ", normalProps);

  // The titlebar draws the battery at the region's far right after status.
}

void View::updateDeferredPlayButtonHold() {
  if (!deferredPlayButtonActive_ || deferredPlayButtonKillTriggered_) {
    return;
  }

  System *system = System::GetInstance();
  if (system == nullptr) {
    return;
  }

  const uint32_t now = static_cast<uint32_t>(system->GetClock());
  if (!HasPlayButtonKillHoldElapsed(deferredPlayButtonActive_,
                                    deferredPlayButtonPressedAt_, now)) {
    return;
  }

  Player *player = Player::GetInstance();
  if (player != nullptr) {
    player->Kill();
  }
  deferredPlayButtonKillTriggered_ = true;
}

// Draw power button UI overlay
void View::drawPowerButtonUI(GUITextProperties &props) {
  // Only process and draw UI when power button is pressed
  if (powerButtonPressed_) {
    char countdownMessage[SCREEN_WIDTH];
    powerButtonHoldCount_++;

    int remainingSeconds = 3 - (powerButtonHoldCount_ / PICO_CLOCK_HZ);
    if (remainingSeconds < 0) {
      remainingSeconds = 0;
    }

    snprintf(countdownMessage, sizeof(countdownMessage),
             "Hold for shutdown (%d sec)", remainingSeconds);

    if (remainingSeconds == 0) {
      Trace::Debug("Power button held for threshold time, Powerdown!");

      // goes via AppWindow so that the project gets auto saved before we
      // actually power off
      static_cast<AppWindow &>(w_).PowerDown();
    }

    // Calculate center position for the message
    GUIPoint pos = GetAnchor();
    uint16_t mesgLen = strlen(countdownMessage);
    pos._x = (SCREEN_WIDTH - mesgLen) / 2;
    pos._y = SCREEN_HEIGHT / 2 - 1;

    auto &window = static_cast<AppWindow &>(w_);
    window.BeginOverlay(AppWindow::PowerOverlayOwner);
    window.SetOverlayBounds({int16_t(8 + (pos._x - 1) * 22),
                             int16_t((pos._y - 1) * 30),
                             int16_t((mesgLen + 3) * 22), 90});
    // Draw a background box
    SetColor(CD_BACKGROUND);
    for (int y = pos._y - 1; y <= pos._y + 1; y++) {
      for (int x = pos._x - 1; x <= (uint16_t)(pos._x + mesgLen + 1); x++) {
        GUITextProperties invProps = props;
        invProps.invert_ = true;
        w_.DrawString(" ", GUIPoint(x, y), invProps);
      }
    }

    // Border stays inside the existing three-row overlay backing rectangle.
    GUITextProperties borderProps = props;
    borderProps.invert_ = false;
    SetColor(CD_ACCENT);
    char border[SCREEN_WIDTH + 1];
    memset(border, 196, mesgLen + 3);
    border[mesgLen + 3] = '\0';
    border[0] = static_cast<char>(218);
    border[mesgLen + 2] = static_cast<char>(191);
    w_.DrawString(border, GUIPoint(pos._x - 1, pos._y - 1), borderProps);
    border[0] = static_cast<char>(192);
    border[mesgLen + 2] = static_cast<char>(217);
    w_.DrawString(border, GUIPoint(pos._x - 1, pos._y + 1), borderProps);
    border[0] = static_cast<char>(179);
    border[1] = '\0';
    w_.DrawString(border, GUIPoint(pos._x - 1, pos._y), borderProps);
    w_.DrawString(border, GUIPoint(pos._x + mesgLen + 1, pos._y), borderProps);

    // Draw the message
    SetColor(CD_EMPHASIS);
    w_.DrawString(countdownMessage, pos, borderProps);
    window.EndOverlay();
  } else if (powerButtonHoldCount_ > 0) {
    // Reset hold counter when button is released
    powerButtonHoldCount_ = 0;

    // Restore the live underlying pixels immediately; no full-view recovery.
    static_cast<AppWindow &>(w_).CloseOverlay(AppWindow::PowerOverlayOwner);
    Trace::Debug("Power button released! Overlay restored.");
  }
}

void View::printHelpLegend(uint8_t region, TrackerCommand command,
                           GUITextProperties props) {
  if (command == TrackerCommand::InstrumentCommandNone) {
    return;
  }

  SetColor(CD_NORMAL);
  HelpLegendPair helpLegend = getHelpLegend(command);
  // Clear both information lines before replacing the command description.
  const TextRegionInfo *info =
      static_cast<AppWindow &>(w_).GetTextRegion(region);
  if (!info)
    return;
  ClearRegionTextRect(region, 0, 0, info->columns, 2);
  DrawRegionString(region, 3, 0, helpLegend[0].c_str(), props);
  if (!helpLegend[1].empty()) {
    DrawRegionString(region, 3, 1, helpLegend[1].c_str(), props);
  }
}

void View::switchToRecordView() {
  // recording view only for the Advance
  if (!Player::GetInstance()->IsRunning()) {
    RecordView::SetSourceViewType(viewType_);
    SampleEditorView::SetSourceViewType(viewType_);
    ViewType vt = VT_RECORD;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  }
}
