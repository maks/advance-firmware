/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Application/Instruments/CommandList.h"
#include "doctest/doctest.h"
#include <cstring>
#include <initializer_list>

namespace {

using NavigateCommand = TrackerCommand (*)(TrackerCommand, CommandContext);

TrackerCommand FindNavigationEnd(CommandContext context,
                                 NavigateCommand navigate) {
  TrackerCommand current = TrackerCommand::InstrumentCommandNone;
  for (uint16_t i = 0; i <= UINT8_MAX; i++) {
    const TrackerCommand next = navigate(current, context);
    CHECK(CommandList::IsCommandAllowed(next, context));
    if (next == current) {
      return current;
    }
    current = next;
  }

  FAIL("Command navigation did not terminate");
  return current;
}

void CheckReverseNavigation(CommandContext context, NavigateCommand forward,
                            NavigateCommand reverse) {
  TrackerCommand current = FindNavigationEnd(context, forward);
  for (uint16_t i = 0; i <= UINT8_MAX; i++) {
    const TrackerCommand previous = reverse(current, context);
    CHECK(CommandList::IsCommandAllowed(previous, context));
    if (previous == current) {
      return;
    }
    current = previous;
  }

  FAIL("Reverse command navigation did not terminate");
}

} // namespace

TEST_CASE("Commands declare whether phrase and table contexts support them") {
  CHECK_FALSE(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandInstrumentRetrigger,
      CommandContext::Phrase));
  CHECK(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandInstrumentRetrigger,
      CommandContext::Table));
  CHECK(CommandList::IsCommandAllowed(TrackerCommand::InstrumentCommandTable,
                                      CommandContext::Phrase));
  CHECK_FALSE(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandTable, CommandContext::Table));
  CHECK(CommandList::IsCommandAllowed(TrackerCommand::InstrumentCommandLegato,
                                      CommandContext::Phrase));
  CHECK_FALSE(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandLegato, CommandContext::Table));

  CHECK(CommandList::IsCommandAllowed(TrackerCommand::InstrumentCommandVolume,
                                      CommandContext::Phrase));
  CHECK(CommandList::IsCommandAllowed(TrackerCommand::InstrumentCommandVolume,
                                      CommandContext::Table));
  CHECK(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandDelayTime, CommandContext::Phrase));
  CHECK(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandDelayTime, CommandContext::Table));
  CHECK(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandDelayFeedback, CommandContext::Phrase));
  CHECK(CommandList::IsCommandAllowed(
      TrackerCommand::InstrumentCommandDelayFeedback, CommandContext::Table));
  for (const TrackerCommand command :
       {TrackerCommand::InstrumentCommandStarLoomTimbre1,
        TrackerCommand::InstrumentCommandStarLoomTimbre2,
        TrackerCommand::InstrumentCommandStarLoomColor1,
        TrackerCommand::InstrumentCommandStarLoomColor2,
        TrackerCommand::InstrumentCommandStarLoomTune1,
        TrackerCommand::InstrumentCommandStarLoomTune2,
        TrackerCommand::InstrumentCommandStarLoomMix}) {
    CHECK(CommandList::IsCommandAllowed(command, CommandContext::Phrase));
    CHECK(CommandList::IsCommandAllowed(command, CommandContext::Table));
  }
}

TEST_CASE("Command navigation only returns commands supported by its context") {
  for (const CommandContext context :
       {CommandContext::Phrase, CommandContext::Table}) {
    FindNavigationEnd(context, CommandList::GetNext);
    FindNavigationEnd(context, CommandList::GetNextAlpha);
    CheckReverseNavigation(context, CommandList::GetNext, CommandList::GetPrev);
    CheckReverseNavigation(context, CommandList::GetNextAlpha,
                           CommandList::GetPrevAlpha);
  }
}

TEST_CASE("StarLoom command IDs and mnemonics are stable") {
  CHECK(static_cast<uint8_t>(
            TrackerCommand::InstrumentCommandStarLoomTimbre1) == 199);
  CHECK(static_cast<uint8_t>(
            TrackerCommand::InstrumentCommandStarLoomTimbre2) == 200);
  CHECK(static_cast<uint8_t>(TrackerCommand::InstrumentCommandStarLoomColor1) ==
        201);
  CHECK(static_cast<uint8_t>(TrackerCommand::InstrumentCommandStarLoomColor2) ==
        202);
  CHECK(static_cast<uint8_t>(TrackerCommand::InstrumentCommandStarLoomTune1) ==
        203);
  CHECK(static_cast<uint8_t>(TrackerCommand::InstrumentCommandStarLoomTune2) ==
        204);
  CHECK(static_cast<uint8_t>(TrackerCommand::InstrumentCommandStarLoomMix) ==
        205);

  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandStarLoomTimbre1)
                .c_str(),
            "ST1") == 0);
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandStarLoomTimbre2)
                .c_str(),
            "ST2") == 0);
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandStarLoomColor1)
                .c_str(),
            "SC1") == 0);
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandStarLoomColor2)
                .c_str(),
            "SC2") == 0);
  CHECK(
      std::strcmp(TrackerCommand(TrackerCommand::InstrumentCommandStarLoomTune1)
                      .c_str(),
                  "SP1") == 0);
  CHECK(
      std::strcmp(TrackerCommand(TrackerCommand::InstrumentCommandStarLoomTune2)
                      .c_str(),
                  "SP2") == 0);
  CHECK(
      std::strcmp(
          TrackerCommand(TrackerCommand::InstrumentCommandStarLoomMix).c_str(),
          "SMX") == 0);

  CHECK(static_cast<uint8_t>(TrackerCommand::InstrumentCommandFilterCut) == 20);
  CHECK(static_cast<uint8_t>(
            TrackerCommand::InstrumentCommandReverbModulationLevel) == 198);
}

TEST_CASE("Delay effect commands clamp only their target byte") {
  CHECK(CommandList::RangeLimitCommandParam(
            TrackerCommand::InstrumentCommandDelayTime, 0xAA00) == 0xAA01);
  CHECK(CommandList::RangeLimitCommandParam(
            TrackerCommand::InstrumentCommandDelayTime, 0xAA60) == 0xAA60);
  CHECK(CommandList::RangeLimitCommandParam(
            TrackerCommand::InstrumentCommandDelayTime, 0xAAFF) == 0xAA60);

  CHECK(CommandList::RangeLimitCommandParam(
            TrackerCommand::InstrumentCommandDelayFeedback, 0xAA00) == 0xAA00);
  CHECK(CommandList::RangeLimitCommandParam(
            TrackerCommand::InstrumentCommandDelayFeedback, 0xAA3F) == 0xAA3F);
  CHECK(CommandList::RangeLimitCommandParam(
            TrackerCommand::InstrumentCommandDelayFeedback, 0xAAFF) == 0xAAFF);
}
