/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef COMMAND_HELP_STATE_H
#define COMMAND_HELP_STATE_H
#include "Foundation/Types/Types.h"

// Retain only the displayed command, not text or pixels. Clearing the view
// invalidates this state so held help can be repainted on the next animation.
class CommandHelpState {
public:
  enum class Change { None, Show, Hide };
  Change Update(bool enterHeld, TrackerCommand command) {
    TrackerCommand next = command;
    if (!enterHeld)
      next = TrackerCommand::InstrumentCommandNone;
    if (next == displayed_)
      return Change::None;
    displayed_ = next;
    return next == TrackerCommand::InstrumentCommandNone ? Change::Hide
                                                         : Change::Show;
  }
  void Reset() { displayed_ = TrackerCommand::InstrumentCommandNone; }

private:
  TrackerCommand displayed_ = TrackerCommand::InstrumentCommandNone;
};
#endif
