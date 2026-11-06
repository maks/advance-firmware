/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "ControlSurface/ApcMiniMk2Surface.h"
#include "Application/AppWindow.h"
#include "Application/Model/Project.h"
#include "Application/Model/Song.h"
#include "Application/Player/Player.h"
#include "ControlSurface/ColorConversion.h"
#include "Foundation/Variables/Variable.h"
#include "Services/Midi/MidiMessage.h"
#include "Services/Midi/MidiService.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace {
constexpr uint16_t kProfileOrderApcRefresh = 82;
constexpr uint16_t kProfileOrderApcBind = kProfileOrderApcRefresh + 1;
constexpr uint16_t kProfileOrderApcIntro = kProfileOrderApcRefresh + 2;
constexpr uint16_t kProfileOrderApcGrid = kProfileOrderApcRefresh + 3;
constexpr uint16_t kProfileOrderApcGridState = kProfileOrderApcRefresh + 4;
constexpr uint16_t kProfileOrderApcGridWrite = kProfileOrderApcRefresh + 5;
constexpr uint16_t kProfileOrderApcTrack = kProfileOrderApcRefresh + 6;
constexpr uint16_t kProfileOrderApcScene = kProfileOrderApcRefresh + 7;
constexpr uint16_t kProfileOrderApcSendRgb = kProfileOrderApcRefresh + 8;
constexpr uint16_t kProfileOrderApcSendSingle = kProfileOrderApcRefresh + 9;
constexpr int kApcGridMinNote = 0x00;
constexpr int kApcGridMaxNote = 0x3F;
constexpr int kApcTrackScrollButtonOffset = 4;
constexpr int kApcShiftNote = 0x7A;
constexpr int kApcFaderMinCc = 0x30;
constexpr int kApcFaderMaxCc = 0x38;

constexpr uint8_t kApcSolidFull = 0x96;
constexpr uint8_t kApcPulse = 0x98;

constexpr uint8_t kApcLedOff = 0x00;
constexpr uint8_t kApcLedOn = 0x01;
constexpr uint8_t kApcLedBlink = 0x02;

constexpr uint8_t kApcColorOff = 0;
constexpr uint8_t kApcColorWhite = 3;
constexpr uint8_t kApcColorRed = 5;
constexpr uint8_t kApcColorAmber = 9;
constexpr uint8_t kApcColorYellow = 13;
constexpr uint8_t kApcColorDimGreen = 22;
constexpr uint8_t kApcColorGreen = 21;
constexpr uint8_t kApcColorCyan = 78;
constexpr uint8_t kApcColorBlue = 45;

constexpr uint8_t kLedCacheInvalid = 0xFF;

constexpr uint8_t kIntroMessage[] = {0xF0, 0x47, 0x7F, 0x4F, 0x60, 0x00,
                                     0x04, 0x00, 0x02, 0x03, 0x00, 0xF7};

struct ApcPaletteEntry {
  uint8_t velocity;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

constexpr ApcPaletteEntry kPalette[] = {
    {.velocity = 0, .red = 0x00, .green = 0x00, .blue = 0x00},
    {.velocity = 1, .red = 0x1E, .green = 0x1E, .blue = 0x1E},
    {.velocity = 2, .red = 0x7F, .green = 0x7F, .blue = 0x7F},
    {.velocity = 3, .red = 0xFF, .green = 0xFF, .blue = 0xFF},
    {.velocity = 4, .red = 0xFF, .green = 0x4C, .blue = 0x4C},
    {.velocity = 5, .red = 0xFF, .green = 0x00, .blue = 0x00},
    {.velocity = 6, .red = 0x59, .green = 0x00, .blue = 0x00},
    {.velocity = 7, .red = 0x19, .green = 0x00, .blue = 0x00},
    {.velocity = 8, .red = 0xFF, .green = 0xBD, .blue = 0x6C},
    {.velocity = 9, .red = 0xFF, .green = 0x54, .blue = 0x00},
    {.velocity = 10, .red = 0x59, .green = 0x1D, .blue = 0x00},
    {.velocity = 11, .red = 0x27, .green = 0x1B, .blue = 0x00},
    {.velocity = 12, .red = 0xFF, .green = 0xFF, .blue = 0x4C},
    {.velocity = 13, .red = 0xFF, .green = 0xFF, .blue = 0x00},
    {.velocity = 14, .red = 0x59, .green = 0x59, .blue = 0x00},
    {.velocity = 15, .red = 0x19, .green = 0x19, .blue = 0x00},
    {.velocity = 16, .red = 0x88, .green = 0xFF, .blue = 0x4C},
    {.velocity = 17, .red = 0x54, .green = 0xFF, .blue = 0x00},
    {.velocity = 18, .red = 0x1D, .green = 0x59, .blue = 0x00},
    {.velocity = 19, .red = 0x14, .green = 0x2B, .blue = 0x00},
    {.velocity = 20, .red = 0x4C, .green = 0xFF, .blue = 0x4C},
    {.velocity = 21, .red = 0x00, .green = 0xFF, .blue = 0x00},
    {.velocity = 22, .red = 0x00, .green = 0x59, .blue = 0x00},
    {.velocity = 23, .red = 0x00, .green = 0x19, .blue = 0x00},
    {.velocity = 24, .red = 0x4C, .green = 0xFF, .blue = 0x5E},
    {.velocity = 25, .red = 0x00, .green = 0xFF, .blue = 0x19},
    {.velocity = 26, .red = 0x00, .green = 0x59, .blue = 0x0D},
    {.velocity = 27, .red = 0x00, .green = 0x19, .blue = 0x02},
    {.velocity = 28, .red = 0x4C, .green = 0xFF, .blue = 0x88},
    {.velocity = 29, .red = 0x00, .green = 0xFF, .blue = 0x55},
    {.velocity = 30, .red = 0x00, .green = 0x59, .blue = 0x1D},
    {.velocity = 31, .red = 0x00, .green = 0x1F, .blue = 0x12},
    {.velocity = 32, .red = 0x4C, .green = 0xFF, .blue = 0xB7},
    {.velocity = 33, .red = 0x00, .green = 0xFF, .blue = 0x99},
    {.velocity = 34, .red = 0x00, .green = 0x59, .blue = 0x35},
    {.velocity = 35, .red = 0x00, .green = 0x19, .blue = 0x12},
    {.velocity = 36, .red = 0x4C, .green = 0xC3, .blue = 0xFF},
    {.velocity = 37, .red = 0x00, .green = 0xA9, .blue = 0xFF},
    {.velocity = 38, .red = 0x00, .green = 0x41, .blue = 0x52},
    {.velocity = 39, .red = 0x00, .green = 0x10, .blue = 0x19},
    {.velocity = 40, .red = 0x4C, .green = 0x88, .blue = 0xFF},
    {.velocity = 41, .red = 0x00, .green = 0x55, .blue = 0xFF},
    {.velocity = 42, .red = 0x00, .green = 0x1D, .blue = 0x59},
    {.velocity = 43, .red = 0x00, .green = 0x08, .blue = 0x19},
    {.velocity = 44, .red = 0x4C, .green = 0x4C, .blue = 0xFF},
    {.velocity = 45, .red = 0x00, .green = 0x00, .blue = 0xFF},
    {.velocity = 46, .red = 0x00, .green = 0x00, .blue = 0x59},
    {.velocity = 47, .red = 0x00, .green = 0x00, .blue = 0x19},
    {.velocity = 48, .red = 0x87, .green = 0x4C, .blue = 0xFF},
    {.velocity = 49, .red = 0x54, .green = 0x00, .blue = 0xFF},
    {.velocity = 50, .red = 0x19, .green = 0x00, .blue = 0x64},
    {.velocity = 51, .red = 0x0F, .green = 0x00, .blue = 0x30},
    {.velocity = 52, .red = 0xFF, .green = 0x4C, .blue = 0xFF},
    {.velocity = 53, .red = 0xFF, .green = 0x00, .blue = 0xFF},
    {.velocity = 54, .red = 0x59, .green = 0x00, .blue = 0x59},
    {.velocity = 55, .red = 0x19, .green = 0x00, .blue = 0x19},
    {.velocity = 56, .red = 0xFF, .green = 0x4C, .blue = 0x87},
    {.velocity = 57, .red = 0xFF, .green = 0x00, .blue = 0x54},
    {.velocity = 58, .red = 0x59, .green = 0x00, .blue = 0x1D},
    {.velocity = 59, .red = 0x22, .green = 0x00, .blue = 0x13},
    {.velocity = 60, .red = 0xFF, .green = 0x15, .blue = 0x00},
    {.velocity = 61, .red = 0x99, .green = 0x35, .blue = 0x00},
    {.velocity = 62, .red = 0x79, .green = 0x51, .blue = 0x00},
    {.velocity = 63, .red = 0x43, .green = 0x64, .blue = 0x00},
    {.velocity = 64, .red = 0x03, .green = 0x39, .blue = 0x00},
    {.velocity = 65, .red = 0x00, .green = 0x57, .blue = 0x35},
    {.velocity = 66, .red = 0x00, .green = 0x54, .blue = 0x7F},
    {.velocity = 67, .red = 0x00, .green = 0x00, .blue = 0xFF},
    {.velocity = 68, .red = 0x00, .green = 0x45, .blue = 0x4F},
    {.velocity = 69, .red = 0x25, .green = 0x00, .blue = 0xCC},
    {.velocity = 70, .red = 0x7F, .green = 0x7F, .blue = 0x7F},
    {.velocity = 71, .red = 0x20, .green = 0x20, .blue = 0x20},
    {.velocity = 72, .red = 0xFF, .green = 0x00, .blue = 0x00},
    {.velocity = 73, .red = 0xBD, .green = 0xFF, .blue = 0x2D},
    {.velocity = 74, .red = 0xAF, .green = 0xED, .blue = 0x06},
    {.velocity = 75, .red = 0x64, .green = 0xFF, .blue = 0x09},
    {.velocity = 76, .red = 0x10, .green = 0x8B, .blue = 0x00},
    {.velocity = 77, .red = 0x00, .green = 0xFF, .blue = 0x87},
    {.velocity = 78, .red = 0x00, .green = 0xA9, .blue = 0xFF},
    {.velocity = 79, .red = 0x00, .green = 0x2A, .blue = 0xFF},
    {.velocity = 80, .red = 0x3F, .green = 0x00, .blue = 0xFF},
    {.velocity = 81, .red = 0x7A, .green = 0x00, .blue = 0xFF},
    {.velocity = 82, .red = 0xB2, .green = 0x1A, .blue = 0x7D},
    {.velocity = 83, .red = 0x40, .green = 0x21, .blue = 0x00},
    {.velocity = 84, .red = 0xFF, .green = 0x4A, .blue = 0x00},
    {.velocity = 85, .red = 0x88, .green = 0xE1, .blue = 0x06},
    {.velocity = 86, .red = 0x72, .green = 0xFF, .blue = 0x15},
    {.velocity = 87, .red = 0x00, .green = 0xFF, .blue = 0x00},
    {.velocity = 88, .red = 0x3B, .green = 0xFF, .blue = 0x26},
    {.velocity = 89, .red = 0x59, .green = 0xFF, .blue = 0x71},
    {.velocity = 90, .red = 0x38, .green = 0xFF, .blue = 0xCC},
    {.velocity = 91, .red = 0x5B, .green = 0x8A, .blue = 0xFF},
    {.velocity = 92, .red = 0x31, .green = 0x51, .blue = 0xC6},
    {.velocity = 93, .red = 0x87, .green = 0x7F, .blue = 0xE9},
    {.velocity = 94, .red = 0xD3, .green = 0x1D, .blue = 0xFF},
    {.velocity = 95, .red = 0xFF, .green = 0x00, .blue = 0x5D},
    {.velocity = 96, .red = 0xFF, .green = 0x7F, .blue = 0x00},
    {.velocity = 97, .red = 0xB9, .green = 0xB0, .blue = 0x00},
    {.velocity = 98, .red = 0x90, .green = 0xFF, .blue = 0x00},
    {.velocity = 99, .red = 0x83, .green = 0x5D, .blue = 0x07},
    {.velocity = 100, .red = 0x39, .green = 0x2B, .blue = 0x00},
    {.velocity = 101, .red = 0x14, .green = 0x4C, .blue = 0x10},
    {.velocity = 102, .red = 0x0D, .green = 0x50, .blue = 0x38},
    {.velocity = 103, .red = 0x15, .green = 0x15, .blue = 0x2A},
    {.velocity = 104, .red = 0x16, .green = 0x20, .blue = 0x5A},
    {.velocity = 105, .red = 0x69, .green = 0x3C, .blue = 0x1C},
    {.velocity = 106, .red = 0xA8, .green = 0x00, .blue = 0x0A},
    {.velocity = 107, .red = 0xDE, .green = 0x51, .blue = 0x3D},
    {.velocity = 108, .red = 0xD8, .green = 0x6A, .blue = 0x1C},
    {.velocity = 109, .red = 0xFF, .green = 0xE1, .blue = 0x26},
    {.velocity = 110, .red = 0x9E, .green = 0xE1, .blue = 0x2F},
    {.velocity = 111, .red = 0x67, .green = 0xB5, .blue = 0x0F},
    {.velocity = 112, .red = 0x1E, .green = 0x1E, .blue = 0x30},
    {.velocity = 113, .red = 0xDC, .green = 0xFF, .blue = 0x6B},
    {.velocity = 114, .red = 0x80, .green = 0xFF, .blue = 0xBD},
    {.velocity = 115, .red = 0x9A, .green = 0x99, .blue = 0xFF},
    {.velocity = 116, .red = 0x8E, .green = 0x66, .blue = 0xFF},
    {.velocity = 117, .red = 0x40, .green = 0x40, .blue = 0x40},
    {.velocity = 118, .red = 0x75, .green = 0x75, .blue = 0x75},
    {.velocity = 119, .red = 0xE0, .green = 0xFF, .blue = 0xFF},
    {.velocity = 120, .red = 0xA0, .green = 0x00, .blue = 0x00},
    {.velocity = 121, .red = 0x35, .green = 0x00, .blue = 0x00},
    {.velocity = 122, .red = 0x1A, .green = 0xD0, .blue = 0x00},
    {.velocity = 123, .red = 0x07, .green = 0x42, .blue = 0x00},
    {.velocity = 124, .red = 0xB9, .green = 0xB0, .blue = 0x00},
    {.velocity = 125, .red = 0x3F, .green = 0x31, .blue = 0x00},
    {.velocity = 126, .red = 0xB3, .green = 0x5F, .blue = 0x00},
    {.velocity = 127, .red = 0x4B, .green = 0x15, .blue = 0x02},
};

int toApcRgbComponent(uint32_t value) {
  return static_cast<int>((value * 255u + 63u) / 127u);
}

FourCC GetChannelVolumeFourCC(int channel) {
  static const FourCC ids[] = {
      FourCC::VarChannel1Volume, FourCC::VarChannel2Volume,
      FourCC::VarChannel3Volume, FourCC::VarChannel4Volume,
      FourCC::VarChannel5Volume, FourCC::VarChannel6Volume,
      FourCC::VarChannel7Volume, FourCC::VarChannel8Volume};
  return ids[channel];
}

int scaleMidiValue(uint8_t value, int maxValue) {
  return static_cast<int>((static_cast<uint32_t>(value) * maxValue + 63u) /
                          127u);
}

} // namespace

ApcMiniMk2Surface::ApcMiniMk2Surface()
    : ControlSurfaceDevice(kApcButtonCount), introduced_(false),
      shiftHeld_(false), baseGridColorsCached_(false),
      emptyColor_(kApcColorOff), noteOffColor_(kApcColorOff),
      firstPatternColor_(kApcColorOff), patternColor_(kApcColorOff) {
  resetLedCache();
}

ApcMiniMk2Surface::~ApcMiniMk2Surface() { Stop(); }

bool ApcMiniMk2Surface::Init(MidiService &midiService) {
  ControlSurfaceDevice::Init(midiService);
  introduced_ = false;
  shiftHeld_ = false;
  baseGridColorsCached_ = false;
  for (auto &state : faderPickup_) {
    state = {};
  }
  resetLedCache();
  return true;
}

bool ApcMiniMk2Surface::OnMidiMessage(MidiMessage &message) {
  if (!isRecognizedApcControl(message)) {
    return false;
  }

  if (!IsEnabled()) {
    return false;
  }

  (void)EnsureRuntimeBindings();

  int channel = 0;
  int row = 0;
  int faderIndex = 0;

  if (decodeFaderCc(message.data1_, faderIndex) &&
      (message.GetType() == MidiMessage::MIDI_CONTROL_CHANGE)) {
    handleFaderMove(faderIndex, message.data2_);
    message.Consume();
    return true;
  }

  if (message.GetType() != MidiMessage::MIDI_NOTE_ON &&
      message.GetType() != MidiMessage::MIDI_NOTE_OFF) {
    return false;
  }

  if (decodeGridPad(message.data1_, channel, row)) {
    if (IsLiveModeActive() && isPress(message)) {
      HandleGridPadPress(channel, row);
      message.Consume();
      return true;
    }
    return false;
  }

  if (decodeTrackButton(message.data1_, channel)) {
    if (!IsLiveModeActive()) {
      return false;
    }

    if (channel < kApcTrackScrollButtonOffset) {
      return false;
    }

    if (isPress(message)) {
      switch (channel - kApcTrackScrollButtonOffset) {
      case kShiftActionFineUp:
        HandleSongWindowFineUpPress();
        break;
      case kShiftActionFineDown:
        HandleSongWindowFineDownPress();
        break;
      case kShiftActionPageUp:
        HandleSongWindowPageUpPress();
        break;
      case kShiftActionPageDown:
        HandleSongWindowPageDownPress();
        break;
      default:
        break;
      }
      message.Consume();
      return true;
    } else if (isRelease(message)) {
      message.Consume();
      return true;
    }
    return false;
  }

  if (decodeSceneButton(message.data1_, row)) {
    if (!IsLiveModeActive()) {
      return false;
    }

    if (isPress(message)) {
      if (!shiftHeld_) {
        HandleSceneLaunchPress(row);
        message.Consume();
        return true;
      }

      switch (row) {
      case kShiftActionFineUp:
        HandleSongWindowFineUpPress();
        message.Consume();
        return true;
      case kShiftActionFineDown:
        HandleSongWindowFineDownPress();
        message.Consume();
        return true;
      case kShiftActionPageUp:
        HandleSongWindowPageUpPress();
        message.Consume();
        return true;
      case kShiftActionPageDown:
        HandleSongWindowPageDownPress();
        message.Consume();
        return true;
      case kShiftActionTransport:
        HandleTransportPress();
        message.Consume();
        return true;
      default:
        return false;
      }
    } else if (isRelease(message)) {
      if (!shiftHeld_) {
        return false;
      }

      switch (row) {
      default:
        return false;
      }
    }
    return false;
  }

  if (isShiftButton(message.data1_)) {
    shiftHeld_ = isPress(message);
    MarkLedsDirty();
    RefreshLeds();
    message.Consume();
    return true;
  }

  return false;
}

void ApcMiniMk2Surface::RefreshLeds() {
  if (!IsStarted() || !IsEnabled() || !AreLedsDirty() ||
      (GetMidiService() == nullptr)) {
    return;
  }

  if (!introduced_) {
    sendIntroductionMessage();
  }

  ControlSurfaceState state;
  GetPlayer()->FillControlSurfaceState(state, GetSongWindowOffset(),
                                       kApcButtonCount);

  if (state.isLiveMode) {
    cacheBaseGridColors();
  }

  uint16_t pos = 0;

  {
    for (int row = 0; row < kApcButtonCount; ++row) {
      for (int channel = 0; channel < kApcButtonCount; ++channel) {
        uint8_t color = kApcColorOff;
        uint8_t behavior = kApcSolidFull;

        if (state.isLiveMode) {
          switch (state.cells[channel][row]) {
          case ControlSurfaceState::Cell::Playing:
            color = kApcColorGreen;
            behavior = kApcPulse;
            break;
          case ControlSurfaceState::Cell::Queued:
            color = kApcColorYellow;
            break;
          case ControlSurfaceState::Cell::Muted:
            color = kApcColorRed;
            break;
          case ControlSurfaceState::Cell::NoteOff:
            color = noteOffColor_;
            break;
          case ControlSurfaceState::Cell::FirstPattern:
            color = firstPatternColor_;
            break;
          case ControlSurfaceState::Cell::Populated:
            color = patternColor_;
            break;
          case ControlSurfaceState::Cell::Empty:
          default:
            color = emptyColor_;
            break;
          }
        }

        if ((gridLedColor_[channel][row] != color) ||
            (gridLedBehavior_[channel][row] != behavior)) {
          gridLedColor_[channel][row] = color;
          gridLedBehavior_[channel][row] = behavior;
          appendMidiMessage(pos, behavior,
                            static_cast<uint8_t>(gridNoteFor(channel, row)),
                            color);
        }
      }
    }
  }

  {
    for (int channel = 0; channel < kApcButtonCount; ++channel) {
      uint8_t value = kApcLedOff;
      if (trackLedValue_[channel] != value) {
        trackLedValue_[channel] = value;
        appendMidiMessage(pos, MidiMessage::MIDI_NOTE_ON,
                          static_cast<uint8_t>(trackNoteFor(channel)), value);
      }
    }
  }

  {
    for (int row = 0; row < kApcButtonCount; ++row) {
      uint8_t value = kApcLedOff;

      if (shiftHeld_) {
        if (row == kShiftActionTransport) {
          value = state.isRunning ? kApcLedBlink : kApcLedOn;
        }
      } else if (state.isLiveMode) {
        const auto rowState = GetRowState(state, kApcButtonCount, row);

        if (rowState.playing)
          value = kApcLedBlink;
        else if (rowState.queued || rowState.playable)
          value = kApcLedOn;
      }

      if (sceneLedValue_[row] != value) {
        sceneLedValue_[row] = value;
        appendMidiMessage(pos, MidiMessage::MIDI_NOTE_ON,
                          static_cast<uint8_t>(sceneNoteFor(row)), value);
      }
    }
  }

  if (pos > 0) {
    sendBufferedMidiMessages(pos);
  }

  ClearLedsDirty();
}

void ApcMiniMk2Surface::OnViewDataChanged(I_ObservableData *data) {
  (void)data;
}

bool ApcMiniMk2Surface::isRecognizedApcControl(
    const MidiMessage &message) const {
  switch (message.status_ & 0xF0) {
  case MidiMessage::MIDI_NOTE_ON:
  case MidiMessage::MIDI_NOTE_OFF: {
    int channel = 0;
    int row = 0;
    return decodeGridPad(message.data1_, channel, row) ||
           decodeTrackButton(message.data1_, channel) ||
           decodeSceneButton(message.data1_, row) ||
           isShiftButton(message.data1_);
  }
  case MidiMessage::MIDI_CONTROL_CHANGE: {
    int faderIndex = 0;
    return decodeFaderCc(message.data1_, faderIndex);
  }
  default:
    return false;
  }
}

bool ApcMiniMk2Surface::decodeGridPad(int note, int &channel, int &row) const {
  if ((note < kApcGridMinNote) || (note > kApcGridMaxNote)) {
    return false;
  }

  channel = note % kApcButtonCount;
  row = matrixRowFromApcRow(note / kApcButtonCount);
  return (row >= 0) && (row < kApcButtonCount);
}

bool ApcMiniMk2Surface::decodeTrackButton(int note, int &channel) const {
  if ((note < kApcTrackButtonMinNote) || (note > kApcTrackButtonMaxNote)) {
    return false;
  }

  channel = note - kApcTrackButtonMinNote;
  return (channel >= 0) && (channel < kApcButtonCount);
}

bool ApcMiniMk2Surface::decodeSceneButton(int note, int &row) const {
  if ((note < kApcSceneButtonMinNote) || (note > kApcSceneButtonMaxNote)) {
    return false;
  }

  row = note - kApcSceneButtonMinNote;
  return (row >= 0) && (row < kApcButtonCount);
}

bool ApcMiniMk2Surface::decodeFaderCc(int cc, int &faderIndex) const {
  if ((cc < kApcFaderMinCc) || (cc > kApcFaderMaxCc)) {
    return false;
  }

  faderIndex = cc - kApcFaderMinCc;
  return (faderIndex >= 0) && (faderIndex < kApcFaderCount);
}

bool ApcMiniMk2Surface::isShiftButton(int note) const {
  return note == kApcShiftNote;
}

void ApcMiniMk2Surface::OnSurfaceStopped() {
  for (auto &state : faderPickup_) {
    state = {};
  }
  clearAllLeds(true);
  introduced_ = false;
  shiftHeld_ = false;
  baseGridColorsCached_ = false;
  resetLedCache();
}

void ApcMiniMk2Surface::cacheBaseGridColors() {
  if (baseGridColorsCached_) {
    return;
  }

  emptyColor_ = kApcColorOff;
  noteOffColor_ =
      velocityForPackedRgb(packGuiColor(AppWindow::GetColor(CD_ACCENT)));
  firstPatternColor_ =
      velocityForPackedRgb(packGuiColor(AppWindow::GetColor(CD_HILITE1)));
  patternColor_ =
      velocityForPackedRgb(packGuiColor(AppWindow::GetColor(CD_NORMAL)));
  baseGridColorsCached_ = true;
}

void ApcMiniMk2Surface::sendIntroductionMessage() {
  if (GetMidiService() == nullptr) {
    return;
  }

  GetMidiService()->SendControlSurfaceRawImmediate(
      kIntroMessage, static_cast<uint8_t>(sizeof(kIntroMessage)));
  introduced_ = true;
}

void ApcMiniMk2Surface::clearAllLeds(bool force) {
  if ((GetMidiService() == nullptr) || (!force && !introduced_)) {
    return;
  }

  uint16_t pos = 0;

  for (int row = 0; row < kApcButtonCount; ++row) {
    for (int channel = 0; channel < kApcButtonCount; ++channel) {
      appendMidiMessage(pos, kApcSolidFull,
                        static_cast<uint8_t>(gridNoteFor(channel, row)),
                        kApcColorOff);
    }
  }

  for (int channel = 0; channel < kApcButtonCount; ++channel) {
    appendMidiMessage(pos, MidiMessage::MIDI_NOTE_ON,
                      static_cast<uint8_t>(trackNoteFor(channel)), kApcLedOff);
  }

  for (int row = 0; row < kApcButtonCount; ++row) {
    appendMidiMessage(pos, MidiMessage::MIDI_NOTE_ON,
                      static_cast<uint8_t>(sceneNoteFor(row)), kApcLedOff);
  }

  if (pos > 0) {
    sendBufferedMidiMessages(pos);
  }
}

void ApcMiniMk2Surface::resetLedCache() {
  for (int row = 0; row < kApcButtonCount; ++row) {
    for (int channel = 0; channel < kApcButtonCount; ++channel) {
      gridLedColor_[channel][row] = kLedCacheInvalid;
      gridLedBehavior_[channel][row] = kLedCacheInvalid;
    }
  }

  for (int channel = 0; channel < kApcButtonCount; ++channel) {
    trackLedValue_[channel] = kLedCacheInvalid;
    sceneLedValue_[channel] = kLedCacheInvalid;
  }
}

void ApcMiniMk2Surface::handleFaderMove(int faderIndex, uint8_t value) {
  if ((faderIndex < 0) || (faderIndex >= kApcFaderCount)) {
    return;
  }

  Player *player = GetPlayer();
  Project *project = (player != nullptr) ? player->GetProject() : nullptr;
  if (project == nullptr) {
    return;
  }

  Variable *variable = project->FindVariable(
      (faderIndex < kApcButtonCount) ? GetChannelVolumeFourCC(faderIndex)
                                     : FourCC::VarMasterVolume);
  if (variable == nullptr) {
    return;
  }

  const int32_t volume = scaleMidiValue(value, Project::MixerVolumeMaximum);
  const int32_t target = variable->GetInt();
  FaderPickupState &state = faderPickup_[faderIndex];

  // A volume change from another control requires pickup again.
  if (target != state.lastWrittenValue) {
    state.pickedUp = false;
  }
  if (volume == target || (state.previousVolume >= 0 &&
                           target >= std::min(state.previousVolume, volume) &&
                           target <= std::max(state.previousVolume, volume))) {
    state.pickedUp = true;
  }
  state.previousVolume = volume;
  if (!state.pickedUp) {
    return;
  }

  variable->SetInt(volume);
  state.lastWrittenValue = volume;
  if (AppWindow *appWindow = AppWindow::GetInstance()) {
    appWindow->SetMixerViewDirty();
  }
}

uint8_t ApcMiniMk2Surface::velocityForPackedRgb(uint32_t rgb) const {
  const int red = toApcRgbComponent((rgb >> 16) & 0x7F);
  const int green = toApcRgbComponent((rgb >> 8) & 0x7F);
  const int blue = toApcRgbComponent(rgb & 0x7F);

  uint32_t bestDistance = UINT32_MAX;
  uint8_t bestVelocity = kApcColorOff;
  for (const auto &entry : kPalette) {
    const int dr = red - entry.red;
    const int dg = green - entry.green;
    const int db = blue - entry.blue;
    const uint32_t distance =
        static_cast<uint32_t>(dr * dr + dg * dg + db * db);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestVelocity = entry.velocity;
    }
  }

  return bestVelocity;
}

void ApcMiniMk2Surface::setTrackLed(int channel, uint8_t value) {
  if ((channel < 0) || (channel >= kApcButtonCount)) {
    return;
  }

  if (trackLedValue_[channel] == value) {
    return;
  }

  trackLedValue_[channel] = value;
  uint16_t pos = 0;
  appendMidiMessage(pos, MidiMessage::MIDI_NOTE_ON,
                    static_cast<uint8_t>(trackNoteFor(channel)), value);
  sendBufferedMidiMessages(pos);
}

void ApcMiniMk2Surface::setSceneLed(int row, uint8_t value) {
  if ((row < 0) || (row >= kApcButtonCount)) {
    return;
  }

  if (sceneLedValue_[row] == value) {
    return;
  }

  sceneLedValue_[row] = value;
  uint16_t pos = 0;
  appendMidiMessage(pos, MidiMessage::MIDI_NOTE_ON,
                    static_cast<uint8_t>(sceneNoteFor(row)), value);
  sendBufferedMidiMessages(pos);
}
