/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "doctest/doctest.h"
#include "Foundation/Types/Types.h"
#include <cstring>

TEST_CASE("TrackerCommand stable persisted enum values") {
  CHECK(TrackerCommand::InstrumentCommandArpeggiator == 0);
  CHECK(TrackerCommand::InstrumentCommandChance == 188);
  CHECK(TrackerCommand::InstrumentCommandDelay == 4);
  CHECK(TrackerCommand::InstrumentCommandNone == 45);
  CHECK(TrackerCommand::InstrumentCommandRandom == 189);
  CHECK(TrackerCommand::InstrumentCommandDelayTime == 190);
  CHECK(TrackerCommand::InstrumentCommandDelayFeedback == 191);
  CHECK(TrackerCommand::InstrumentCommandTable == 58);
  CHECK(TrackerCommand::InstrumentCommandOff == 92);
  CHECK(TrackerCommand::InstrumentCommandMidiChord == 143);
}

TEST_CASE("Delay effect command mnemonics stay stable") {
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandDelayTime).c_str(),
            "DLT") == 0);
  CHECK(
      std::strcmp(TrackerCommand(TrackerCommand::InstrumentCommandDelayFeedback)
                      .c_str(),
                  "DLF") == 0);
}

TEST_CASE("Reverb effect command mnemonics stay stable") {
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandReverbPreDelay)
                .c_str(),
            "RVP") == 0);
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandReverbInputTone)
                .c_str(),
            "RVI") == 0);
  CHECK(
      std::strcmp(TrackerCommand(TrackerCommand::InstrumentCommandReverbDiffuse)
                      .c_str(),
                  "RVF") == 0);
  CHECK(
      std::strcmp(
          TrackerCommand(TrackerCommand::InstrumentCommandReverbDecay).c_str(),
          "RVD") == 0);
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandReverbTailTone)
                .c_str(),
            "RVT") == 0);
  CHECK(std::strcmp(TrackerCommand(
                        TrackerCommand::InstrumentCommandReverbModulationSpeed)
                        .c_str(),
                    "RVS") == 0);
  CHECK(std::strcmp(TrackerCommand(
                        TrackerCommand::InstrumentCommandReverbModulationLevel)
                        .c_str(),
                    "RVL") == 0);
}

TEST_CASE("Off command mnemonic is REL") {
  CHECK(
      std::strcmp(TrackerCommand(TrackerCommand::InstrumentCommandOff).c_str(),
                  "REL") == 0);
}

TEST_CASE("Chance and random command mnemonics stay stable") {
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandChance).c_str(),
            "CHN") == 0);
  CHECK(std::strcmp(
            TrackerCommand(TrackerCommand::InstrumentCommandRandom).c_str(),
            "RND") == 0);
}

TEST_CASE("Runtime FourCC uses a wider unsigned storage type") {
  CHECK(sizeof(TrackerCommand) == 1);
  CHECK(sizeof(TrackerCommand::value_type) == 1);
  CHECK(sizeof(FourCC) == 2);
  CHECK(sizeof(FourCC::value_type) == 2);

  FourCC id = FourCC(static_cast<FourCC::value_type>(300));
  CHECK(id.get_value() == 300);
}

TEST_CASE("Runtime FourCC reflects variable names") {
  CHECK(std::strcmp(FourCC(FourCC::SampleSourceInstrumentLevel).c_str(),
                    "level") == 0);
  CHECK(std::strcmp(FourCC(FourCC::VarOutputVolume).c_str(), "outputvolume") ==
        0);
}
