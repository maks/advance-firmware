/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/input/InputRepeatTracker.h"
#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

namespace {
struct QueuedAction {
  uint16_t action;
  bool pressed;
};

std::vector<QueuedAction> gQueuedActions;

bool TestQueueAction(uint16_t action, bool pressed) {
  gQueuedActions.push_back(QueuedAction{action, pressed});
  return true;
}

void ResetTestInputState() {
  gQueuedActions.clear();
  InputMap::SetQueueForTesting(TestQueueAction);
  InputMap::ReleaseAllActions();
  gQueuedActions.clear();
}
} // namespace

TEST_CASE("InputMap press and release tracking") {
  ResetTestInputState();

  CHECK(InputMap::GetHeldActionMask() == 0);
  CHECK(InputMap::GetDispatchedActionMask() == 0);

  // Press action 0 (LEFT)
  CHECK(InputMap::SetAction(0, true));
  CHECK(InputMap::GetHeldActionMask() == (1u << 0));
  REQUIRE(gQueuedActions.size() == 1);
  CHECK(gQueuedActions[0].action == 0);
  CHECK(gQueuedActions[0].pressed == true);

  // Redundant press of action 0 should not produce another queue event
  gQueuedActions.clear();
  CHECK(InputMap::SetAction(0, true));
  CHECK(gQueuedActions.empty());
  CHECK(InputMap::GetHeldActionMask() == (1u << 0));

  // Press action 5 (EDIT - KeyX)
  CHECK(InputMap::SetAction(5, true));
  CHECK(InputMap::GetHeldActionMask() == ((1u << 0) | (1u << 5)));
  REQUIRE(gQueuedActions.size() == 1);
  CHECK(gQueuedActions[0].action == 5);
  CHECK(gQueuedActions[0].pressed == true);

  // Release action 0
  gQueuedActions.clear();
  CHECK(InputMap::SetAction(0, false));
  CHECK(InputMap::GetHeldActionMask() == (1u << 5));
  REQUIRE(gQueuedActions.size() == 1);
  CHECK(gQueuedActions[0].action == 0);
  CHECK(gQueuedActions[0].pressed == false);

  // Release action 5
  gQueuedActions.clear();
  CHECK(InputMap::SetAction(5, false));
  CHECK(InputMap::GetHeldActionMask() == 0);
  REQUIRE(gQueuedActions.size() == 1);
  CHECK(gQueuedActions[0].action == 5);
  CHECK(gQueuedActions[0].pressed == false);

  ResetTestInputState();
}

TEST_CASE("InputMap ReleaseAllActions releases all held keys") {
  ResetTestInputState();

  CHECK(InputMap::SetAction(1, true)); // DOWN
  CHECK(InputMap::SetAction(4, true)); // ALT
  CHECK(InputMap::SetAction(7, true)); // NAV
  CHECK(InputMap::GetHeldActionMask() == ((1u << 1) | (1u << 4) | (1u << 7)));

  gQueuedActions.clear();
  InputMap::ReleaseAllActions();
  CHECK(InputMap::GetHeldActionMask() == 0);

  REQUIRE(gQueuedActions.size() == 3);
  CHECK(gQueuedActions[0].action == 1);
  CHECK(gQueuedActions[0].pressed == false);
  CHECK(gQueuedActions[1].action == 4);
  CHECK(gQueuedActions[1].pressed == false);
  CHECK(gQueuedActions[2].action == 7);
  CHECK(gQueuedActions[2].pressed == false);

  ResetTestInputState();
}

TEST_CASE("InputMap RepeatAction queues only when held") {
  ResetTestInputState();

  // Repeating an unheld action must fail
  CHECK_FALSE(InputMap::RepeatAction(2));
  CHECK(gQueuedActions.empty());

  // Hold action 2 (RIGHT)
  CHECK(InputMap::SetAction(2, true));
  gQueuedActions.clear();

  // Repeat should succeed
  CHECK(InputMap::RepeatAction(2));
  REQUIRE(gQueuedActions.size() == 1);
  CHECK(gQueuedActions[0].action == 2);
  CHECK(gQueuedActions[0].pressed == true);

  // After releasing, repeat should fail again
  CHECK(InputMap::SetAction(2, false));
  gQueuedActions.clear();
  CHECK_FALSE(InputMap::RepeatAction(2));
  CHECK(gQueuedActions.empty());

  ResetTestInputState();
}

TEST_CASE("InputMap DecodeActionEvent and AcknowledgeAction") {
  ResetTestInputState();

  const uint32_t genBefore = InputMap::GetDispatchGeneration();
  InputMap::AcknowledgeAction(7, true);
  CHECK(InputMap::GetDispatchedActionMask() == (1u << 7));
  CHECK(InputMap::GetLastDispatchedAction() == 7);
  CHECK(InputMap::GetDispatchGeneration() == genBefore + 1);

  InputMap::AcknowledgeAction(7, false);
  CHECK(InputMap::GetDispatchedActionMask() == 0);
  CHECK(InputMap::GetLastDispatchedAction() == 7);
  CHECK(InputMap::GetDispatchGeneration() == genBefore + 2);

  // Decode valid encoded action
  uint16_t action = 0;
  bool pressed = false;
  std::uintptr_t encoded = (7u << 1u) | 1u;
  CHECK(InputMap::DecodeActionEvent(encoded, action, pressed));
  CHECK(action == 7);
  CHECK(pressed == true);

  encoded = (2u << 1u) | 0u;
  CHECK(InputMap::DecodeActionEvent(encoded, action, pressed));
  CHECK(action == 2);
  CHECK(pressed == false);

  // Decode out-of-range action
  encoded = (99u << 1u) | 1u;
  CHECK_FALSE(InputMap::DecodeActionEvent(encoded, action, pressed));

  ResetTestInputState();
}

TEST_CASE("InputRepeatTracker directional repeat timing and bounding") {
  InputRepeatTracker tracker(500.0, 30.0);
  std::vector<uint16_t> repeatedActions;

  auto onRepeat = [&](uint16_t a) { repeatedActions.push_back(a); };

  // Non-directional action (e.g. 5: EDIT or 7: NAV) must not be tracked
  tracker.OnAction(5, true, 1000.0);
  tracker.Process(2000.0, onRepeat);
  CHECK(repeatedActions.empty());

  // Press direction action 2 (RIGHT) at t = 1000.0
  tracker.OnAction(2, true, 1000.0);
  const auto &states = tracker.GetStates();
  CHECK(states[2].isHeld == true);
  CHECK(states[2].isRepeating == false);
  CHECK(states[2].lastTriggerTime == 1000.0);

  // At t = 1200.0 (200ms elapsed), delay is 500ms -> should not trigger
  tracker.Process(1200.0, onRepeat);
  CHECK(repeatedActions.empty());
  CHECK(states[2].isRepeating == false);

  // At t = 1500.0 (500ms elapsed) -> should trigger first repeat
  tracker.Process(1500.0, onRepeat);
  REQUIRE(repeatedActions.size() == 1);
  CHECK(repeatedActions[0] == 2);
  CHECK(states[2].isRepeating == true);
  CHECK(states[2].lastTriggerTime == 1500.0);

  // At t = 1520.0 (20ms after first repeat, repeat interval is 30ms) -> no
  // trigger
  repeatedActions.clear();
  tracker.Process(1520.0, onRepeat);
  CHECK(repeatedActions.empty());

  // At t = 1530.0 (30ms after first repeat) -> should trigger second repeat
  tracker.Process(1530.0, onRepeat);
  REQUIRE(repeatedActions.size() == 1);
  CHECK(repeatedActions[0] == 2);
  CHECK(states[2].lastTriggerTime == 1530.0);

  // Bounded execution: simulate background tab returning after 5000ms
  // It must fire at most once and advance lastTriggerTime to current time
  repeatedActions.clear();
  tracker.Process(6530.0, onRepeat);
  REQUIRE(repeatedActions.size() == 1);
  CHECK(repeatedActions[0] == 2);
  CHECK(states[2].lastTriggerTime == 6530.0);

  // Releasing action 2 resets repeat state
  repeatedActions.clear();
  tracker.OnAction(2, false, 6540.0);
  CHECK(states[2].isHeld == false);
  CHECK(states[2].isRepeating == false);
  tracker.Process(7000.0, onRepeat);
  CHECK(repeatedActions.empty());
}

TEST_CASE("InputRepeatTracker SyncHeldMask releases repeat on external clear") {
  InputRepeatTracker tracker(500.0, 30.0);
  std::vector<uint16_t> repeatedActions;
  auto onRepeat = [&](uint16_t a) { repeatedActions.push_back(a); };

  // Press UP (action 3) at t = 1000.0
  tracker.OnAction(3, true, 1000.0);
  CHECK(tracker.GetStates()[3].isHeld == true);

  // External release all: heldMask is now 0
  tracker.SyncHeldMask(0);
  CHECK(tracker.GetStates()[3].isHeld == false);

  // No repeat after delay
  tracker.Process(2000.0, onRepeat);
  CHECK(repeatedActions.empty());
}
