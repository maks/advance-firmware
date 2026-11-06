/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#pragma once

#include "ControlSurfaceDevice.h"
#include <stdint.h>

class MidiService;
class Song;
struct MidiMessage;

class ApcMiniMk2Surface : public ControlSurfaceDevice {
public:
  ApcMiniMk2Surface();
  ~ApcMiniMk2Surface() override;

  bool Init(MidiService &midiService) override;

  bool OnMidiMessage(MidiMessage &message) override;
  void RefreshLeds() override;

private:
  static constexpr int kApcButtonCount = 8;
  static constexpr int kApcFaderCount = 9;
  static constexpr int kApcTransportSceneRow = kApcButtonCount - 1;

  static constexpr int kApcTrackButtonMinNote = 0x64;
  static constexpr int kApcTrackButtonMaxNote = 0x6B;
  static constexpr int kApcSceneButtonMinNote = 0x70;
  static constexpr int kApcSceneButtonMaxNote = 0x77;

  enum SceneShiftAction {
    kShiftActionFineUp = 0,
    kShiftActionFineDown = 1,
    kShiftActionPageUp = 2,
    kShiftActionPageDown = 3,
    kShiftActionUnused4 = 4,
    kShiftActionUnused5 = 5,
    kShiftActionUnused6 = 6,
    kShiftActionTransport = 7,
  };

  bool isRecognizedApcControl(const MidiMessage &message) const;
  bool decodeGridPad(int note, int &channel, int &row) const;
  bool decodeTrackButton(int note, int &channel) const;
  bool decodeSceneButton(int note, int &row) const;
  bool decodeFaderCc(int cc, int &faderIndex) const;
  bool isShiftButton(int note) const;
  void OnViewDataChanged(I_ObservableData *data) override;
  void OnSurfaceStopped() override;

  void cacheBaseGridColors();
  void sendIntroductionMessage();
  void clearAllLeds(bool force = false);
  void resetLedCache();

  void handleFaderMove(int faderIndex, uint8_t value);
  uint8_t velocityForPackedRgb(uint32_t rgb) const;
  void setTrackLed(int channel, uint8_t value);
  void setSceneLed(int row, uint8_t value);

  inline int matrixRowFromApcRow(int apcRow) const {
    return (kApcButtonCount - 1) - apcRow;
  }
  inline int apcRowFromMatrixRow(int row) const {
    return (kApcButtonCount - 1) - row;
  }
  inline int gridNoteFor(int channel, int row) const {
    return apcRowFromMatrixRow(row) * kApcButtonCount + channel;
  }
  inline int trackNoteFor(int channel) const {
    return kApcTrackButtonMinNote + channel;
  }
  inline int sceneNoteFor(int row) const {
    return kApcSceneButtonMinNote + row;
  }

private:
  struct FaderPickupState {
    int32_t previousVolume = -1;
    int32_t lastWrittenValue = -1;
    bool pickedUp = false;
  };

  bool introduced_;
  bool shiftHeld_;
  bool baseGridColorsCached_;
  uint8_t emptyColor_;
  uint8_t noteOffColor_;
  uint8_t firstPatternColor_;
  uint8_t patternColor_;
  uint8_t gridLedColor_[kApcButtonCount][kApcButtonCount];
  uint8_t gridLedBehavior_[kApcButtonCount][kApcButtonCount];
  uint8_t trackLedValue_[kApcButtonCount];
  uint8_t sceneLedValue_[kApcButtonCount];
  FaderPickupState faderPickup_[kApcFaderCount];
};
