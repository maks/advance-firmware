/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#include "Application/Views/BaseClasses/CommandHelpState.h"
#include "doctest/doctest.h"

using Change = CommandHelpState::Change;

TEST_CASE("Command help follows ENTER hold without repeated repaints") {
  CommandHelpState state;
  const TrackerCommand command = TrackerCommand::InstrumentCommandPan;
  CHECK(state.Update(false, command) == Change::None);
  CHECK(state.Update(true, command) == Change::Show);
  // Other key events with ENTER still held must not hide or repaint help.
  CHECK(state.Update(true, command) == Change::None);
  CHECK(state.Update(false, command) == Change::Hide);
  CHECK(state.Update(false, command) == Change::None);
  CHECK(state.Update(true, command) == Change::Show);
}

TEST_CASE(
    "Held help follows edits and clears for an empty or ineligible field") {
  CommandHelpState state;
  CHECK(state.Update(true, TrackerCommand::InstrumentCommandPan) ==
        Change::Show);
  CHECK(state.Update(true, TrackerCommand::InstrumentCommandHop) ==
        Change::Show);
  CHECK(state.Update(true, TrackerCommand::InstrumentCommandNone) ==
        Change::Hide);
  CHECK(state.Update(true, TrackerCommand::InstrumentCommandNone) ==
        Change::None);
  CHECK(state.Update(false, TrackerCommand::InstrumentCommandHop) ==
        Change::None);
}

TEST_CASE("Full view redraw invalidates help without latching ENTER") {
  CommandHelpState state;
  const TrackerCommand command = TrackerCommand::InstrumentCommandPan;
  CHECK(state.Update(true, command) == Change::Show);
  state.Reset();
  CHECK(state.Update(true, command) == Change::Show);
  state.Reset();
  CHECK(state.Update(false, command) == Change::None);
}
