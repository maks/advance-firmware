/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _MIDI_VOICE_PICKUP_H_
#define _MIDI_VOICE_PICKUP_H_

#include <cstdint>

// Tracks one MIDI CC's pickup state for one input source and MIDI channel.
struct MidiVoicePickup {
  int32_t previousValue_ = 0;
  int32_t targetValue_ = 0;
  int32_t lastAppliedValue_ = 0;
  int16_t minimum_ = 0;
  int16_t maximum_ = 0;
  uint8_t instrument_ = 0;
  bool seen_ = false;
  bool acquired_ = false;
  bool hasAppliedValue_ = false;

  void Reset() { *this = {}; }

  bool Accept(int32_t incomingValue, int32_t currentValue, int32_t minimum,
              int32_t maximum, uint8_t instrument) {
    if (!seen_ || instrument_ != instrument || minimum_ != minimum ||
        maximum_ != maximum || (!acquired_ && currentValue != targetValue_) ||
        (acquired_ && hasAppliedValue_ && currentValue != lastAppliedValue_)) {
      previousValue_ = incomingValue;
      targetValue_ = currentValue;
      minimum_ = static_cast<int16_t>(minimum);
      maximum_ = static_cast<int16_t>(maximum);
      instrument_ = instrument;
      seen_ = true;
      acquired_ = false;
      hasAppliedValue_ = false;
      return false;
    }

    if (!acquired_) {
      const int32_t tolerance = maximum > minimum ? 2 : 0;
      const bool nearTarget = incomingValue >= targetValue_ - tolerance &&
                              incomingValue <= targetValue_ + tolerance;
      const bool crossedTarget =
          (previousValue_ <= targetValue_ && incomingValue >= targetValue_) ||
          (previousValue_ >= targetValue_ && incomingValue <= targetValue_);
      previousValue_ = incomingValue;
      if (!nearTarget && !crossedTarget) {
        return false;
      }
      acquired_ = true;
    }

    lastAppliedValue_ = incomingValue;
    hasAppliedValue_ = true;
    return true;
  }
};

#endif
