/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef PICOTRACKER_WASM_INPUT_REPEAT_TRACKER_H
#define PICOTRACKER_WASM_INPUT_REPEAT_TRACKER_H

#include <array>
#include <cstdint>
#include <functional>

class InputRepeatTracker final {
public:
  static constexpr uint16_t DirectionActionCount = 4;
  static constexpr double DefaultKeyDelayMs = 500.0;
  static constexpr double DefaultKeyRepeatMs = 30.0;

  struct RepeatState {
    bool isHeld = false;
    bool isRepeating = false;
    double lastTriggerTime = 0.0;
  };

  using RepeatCallback = std::function<void(uint16_t action)>;

  InputRepeatTracker(double keyDelay = DefaultKeyDelayMs,
                     double keyRepeat = DefaultKeyRepeatMs);

  void SetTiming(double keyDelay, double keyRepeat);
  void OnAction(uint16_t action, bool pressed, double now);
  void SyncHeldMask(uint16_t heldMask);
  void Process(double now, const RepeatCallback &onRepeat);

  const std::array<RepeatState, DirectionActionCount> &GetStates() const {
    return states_;
  }
  double GetKeyDelay() const { return keyDelay_; }
  double GetKeyRepeat() const { return keyRepeat_; }

private:
  double keyDelay_ = DefaultKeyDelayMs;
  double keyRepeat_ = DefaultKeyRepeatMs;
  std::array<RepeatState, DirectionActionCount> states_{};
};

#endif
