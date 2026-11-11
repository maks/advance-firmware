/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Adapters/wasm/input/InputRepeatTracker.h"

InputRepeatTracker::InputRepeatTracker(double keyDelay, double keyRepeat)
    : keyDelay_(keyDelay > 0.0 ? keyDelay : DefaultKeyDelayMs),
      keyRepeat_(keyRepeat > 0.0 ? keyRepeat : DefaultKeyRepeatMs) {}

void InputRepeatTracker::SetTiming(double keyDelay, double keyRepeat) {
  if (keyDelay > 0.0) {
    keyDelay_ = keyDelay;
  }
  if (keyRepeat > 0.0) {
    keyRepeat_ = keyRepeat;
  }
}

void InputRepeatTracker::OnAction(uint16_t action, bool pressed, double now) {
  if (action >= DirectionActionCount) {
    return;
  }
  if (pressed) {
    states_[action].isHeld = true;
    states_[action].isRepeating = false;
    states_[action].lastTriggerTime = now;
  } else {
    states_[action].isHeld = false;
    states_[action].isRepeating = false;
    states_[action].lastTriggerTime = 0.0;
  }
}

void InputRepeatTracker::SyncHeldMask(uint16_t heldMask) {
  for (uint16_t action = 0; action < DirectionActionCount; ++action) {
    if ((heldMask & static_cast<uint16_t>(1u << action)) == 0) {
      states_[action].isHeld = false;
      states_[action].isRepeating = false;
      states_[action].lastTriggerTime = 0.0;
    }
  }
}

void InputRepeatTracker::Process(double now, const RepeatCallback &onRepeat) {
  for (uint16_t action = 0; action < DirectionActionCount; ++action) {
    if (!states_[action].isHeld) {
      continue;
    }
    const double delay = states_[action].isRepeating ? keyRepeat_ : keyDelay_;
    if (now - states_[action].lastTriggerTime >= delay) {
      states_[action].isRepeating = true;
      states_[action].lastTriggerTime = now;
      if (onRepeat) {
        onRepeat(action);
      }
    }
  }
}
