/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "doctest/doctest.h"

#include "Application/Audio/AudioSample.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Services/Midi/MidiService.h"
#include <array>
#include <cstddef>

void ClearQueuedMidiMessagesForTest();
size_t GetQueuedMidiMessageCountForTest();
const MidiMessage &GetQueuedMidiMessageForTest(size_t index);

namespace {
struct MidiServiceScope {
  MidiService service;

  MidiServiceScope() {
    MidiService::Install(&service);
    ClearQueuedMidiMessagesForTest();
  }

  ~MidiServiceScope() {
    ClearQueuedMidiMessagesForTest();
    MidiService::Install(nullptr);
  }
};
} // namespace

TEST_CASE("MidiInstrument continue sends note on before previous note off") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};

  REQUIRE(instrument.Init());
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));

  ClearQueuedMidiMessagesForTest();

  REQUIRE(instrument.Start(0, 62, InstrumentStartMode::TiedNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));

  REQUIRE(GetQueuedMidiMessageCountForTest() == 2);

  const MidiMessage &noteOn = GetQueuedMidiMessageForTest(0);
  CHECK(noteOn.status_ == MidiMessage::MIDI_NOTE_ON);
  CHECK(noteOn.data1_ == 62);
  CHECK(noteOn.data2_ == INITIAL_NOTE_VELOCITY);

  const MidiMessage &noteOff = GetQueuedMidiMessageForTest(1);
  CHECK(noteOff.status_ == MidiMessage::MIDI_NOTE_OFF);
  CHECK(noteOff.data1_ == 60);
  CHECK(noteOff.data2_ == 0x00);
}

TEST_CASE("MidiInstrument continue on same note is a no-op") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};

  REQUIRE(instrument.Init());
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));

  ClearQueuedMidiMessagesForTest();

  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::TiedNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));

  CHECK(GetQueuedMidiMessageCountForTest() == 0);
}

TEST_CASE("MidiInstrument TLN command updates remaining ticks") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};

  REQUIRE(instrument.Init());
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));

  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandGateLength, 6);

  ClearQueuedMidiMessagesForTest();

  // Tick 1 to 6 - should still be playing
  for (int i = 1; i <= 6; ++i) {
    REQUIRE_FALSE(
        instrument.Render(0, buffer.data(), 1, false)); // Should not advance
    CHECK(GetQueuedMidiMessageCountForTest() == 0);

    REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, true));
    CHECK(GetQueuedMidiMessageCountForTest() == 0);
  }

  // Tick 7 - should stop
  REQUIRE_FALSE(
      instrument.Render(0, buffer.data(), 1, false)); // Should not advance
  CHECK(GetQueuedMidiMessageCountForTest() == 0);

  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, true));
  CHECK(GetQueuedMidiMessageCountForTest() == 1);

  const MidiMessage &noteOff = GetQueuedMidiMessageForTest(0);
  CHECK(noteOff.status_ == MidiMessage::MIDI_NOTE_OFF);
  CHECK(noteOff.data1_ == 60);
}

TEST_CASE("MidiInstrument VOL zero speed sends an immediate CC7 value") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};

  REQUIRE(instrument.Init());
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));
  ClearQueuedMidiMessagesForTest();

  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandVolume, 0x0080);

  REQUIRE(GetQueuedMidiMessageCountForTest() == 1);
  const MidiMessage &volume = GetQueuedMidiMessageForTest(0);
  CHECK(volume.status_ == MidiMessage::MIDI_CONTROL_CHANGE);
  CHECK(volume.data1_ == MidiCC::CC_VOLUME);
  CHECK(volume.data2_ == 0x40);
}

TEST_CASE("MidiInstrument VOL speed is an exact tick duration") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};

  REQUIRE(instrument.Init());
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));
  ClearQueuedMidiMessagesForTest();

  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandVolume, 0x0400);
  CHECK(GetQueuedMidiMessageCountForTest() == 0);

  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));
  CHECK(GetQueuedMidiMessageCountForTest() == 0);

  constexpr uint8_t expected[] = {0x5F, 0x3F, 0x1F, 0x00};
  for (uint8_t value : expected) {
    REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, true));
    REQUIRE(GetQueuedMidiMessageCountForTest() == 1);
    const MidiMessage &volume = GetQueuedMidiMessageForTest(0);
    CHECK(volume.status_ == MidiMessage::MIDI_CONTROL_CHANGE);
    CHECK(volume.data1_ == MidiCC::CC_VOLUME);
    CHECK(volume.data2_ == value);
    ClearQueuedMidiMessagesForTest();
  }

  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, true));
  CHECK(GetQueuedMidiMessageCountForTest() == 0);
}

TEST_CASE("MidiInstrument VOL retargets from its current value") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};

  REQUIRE(instrument.Init());
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));
  ClearQueuedMidiMessagesForTest();

  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandVolume, 0x0400);
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, true));
  REQUIRE(GetQueuedMidiMessageCountForTest() == 1);
  CHECK(GetQueuedMidiMessageForTest(0).data2_ == 0x5F);
  ClearQueuedMidiMessagesForTest();

  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandVolume, 0x02FF);
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, true));
  REQUIRE(GetQueuedMidiMessageCountForTest() == 1);
  CHECK(GetQueuedMidiMessageForTest(0).data2_ == 0x6F);
  ClearQueuedMidiMessagesForTest();

  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, true));
  REQUIRE(GetQueuedMidiMessageCountForTest() == 1);
  CHECK(GetQueuedMidiMessageForTest(0).data2_ == 0x7F);
}

TEST_CASE("MidiInstrument tied notes preserve VOL and clean notes reset it") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};

  REQUIRE(instrument.Init());
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandVolume, 0x0000);
  ClearQueuedMidiMessagesForTest();

  REQUIRE(instrument.Start(0, 62, InstrumentStartMode::TiedNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));
  REQUIRE(GetQueuedMidiMessageCountForTest() == 2);
  CHECK((GetQueuedMidiMessageForTest(0).status_ & 0xF0) ==
        MidiMessage::MIDI_NOTE_ON);
  CHECK((GetQueuedMidiMessageForTest(1).status_ & 0xF0) ==
        MidiMessage::MIDI_NOTE_OFF);

  instrument.Stop(0);
  ClearQueuedMidiMessagesForTest();

  REQUIRE(instrument.Start(0, 64, InstrumentStartMode::NewNote));
  REQUIRE_FALSE(instrument.Render(0, buffer.data(), 1, false));
  REQUIRE(GetQueuedMidiMessageCountForTest() == 2);
  const MidiMessage &volume = GetQueuedMidiMessageForTest(0);
  CHECK(volume.status_ == MidiMessage::MIDI_CONTROL_CHANGE);
  CHECK(volume.data1_ == MidiCC::CC_VOLUME);
  CHECK(volume.data2_ == 0x7F);
  CHECK((GetQueuedMidiMessageForTest(1).status_ & 0xF0) ==
        MidiMessage::MIDI_NOTE_ON);
}

TEST_CASE("MidiInstrument sends a configured zero volume on playback start") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  Variable *volume = instrument.FindVariable(FourCC::MidiInstrumentVolume);
  REQUIRE(volume != nullptr);
  volume->SetInt(0);

  instrument.OnStart();

  REQUIRE(GetQueuedMidiMessageCountForTest() == 1);
  const MidiMessage &message = GetQueuedMidiMessageForTest(0);
  CHECK(message.status_ == MidiMessage::MIDI_CONTROL_CHANGE);
  CHECK(message.data1_ == MidiCC::CC_VOLUME);
  CHECK(message.data2_ == 0);
}

TEST_CASE("MidiInstrument IRT retains clean MIDI retrigger behavior") {
  MidiServiceScope midiScope;
  MidiInstrument instrument;
  std::array<AudioSample, 2> buffer = {};
  REQUIRE(instrument.Init());
  instrument.FindVariable(FourCC::MidiInstrumentNoteLength)->SetInt(0);
  REQUIRE(instrument.Start(0, 60, InstrumentStartMode::NewNote));
  instrument.Render(0, buffer.data(), 1, false);
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandVolume, 0x0400);
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandVelocity,
                            0x0040);
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandRetrigger, 2);
  instrument.Render(0, buffer.data(), 1, true);
  ClearQueuedMidiMessagesForTest();

  // PlayerChannel stops MIDI before starting IRT, as for an explicit note.
  instrument.Stop(0);
  REQUIRE(instrument.Start(0, 62, InstrumentStartMode::Retrigger));
  instrument.Render(0, buffer.data(), 1, false);
  REQUIRE(GetQueuedMidiMessageCountForTest() == 3);
  CHECK(GetQueuedMidiMessageForTest(0).status_ == MidiMessage::MIDI_NOTE_OFF);
  CHECK(GetQueuedMidiMessageForTest(1).data1_ == MidiCC::CC_VOLUME);
  CHECK(GetQueuedMidiMessageForTest(1).data2_ == 127);
  CHECK(GetQueuedMidiMessageForTest(2).status_ == MidiMessage::MIDI_NOTE_ON);
  CHECK(GetQueuedMidiMessageForTest(2).data2_ == 127);
  ClearQueuedMidiMessagesForTest();
  for (int tick = 0; tick < 4; ++tick) {
    instrument.Render(0, buffer.data(), 1, true);
  }
  CHECK(GetQueuedMidiMessageCountForTest() == 0);
}

namespace {
class FakeTimerService : public TimerService {
public:
  I_Timer *CreateTimer() override { return nullptr; }
  void TriggerCallback(int msec, timerCallback cb) override {
    lastMsec = msec;
    lastCallback = cb;
  }

  int lastMsec = 0;
  timerCallback lastCallback = nullptr;
};

struct TimerServiceScope {
  FakeTimerService service;

  TimerServiceScope() { TimerService::Install(&service); }
  ~TimerServiceScope() { TimerService::Install(nullptr); }
};
} // namespace

TEST_CASE("MidiInstrument SendProgramChangeWithNote sends PC, NoteOn, and scheduled NoteOff") {
  MidiServiceScope midiScope;
  TimerServiceScope timerScope;
  MidiInstrument instrument;

  REQUIRE(instrument.Init());
  ClearQueuedMidiMessagesForTest();

  instrument.SendProgramChangeWithNote(3, 42);

  REQUIRE(GetQueuedMidiMessageCountForTest() == 2);
  const MidiMessage &pc = GetQueuedMidiMessageForTest(0);
  CHECK(pc.status_ == (MidiMessage::MIDI_PROGRAM_CHANGE + 3));
  CHECK(pc.data1_ == 42);

  const MidiMessage &noteOn = GetQueuedMidiMessageForTest(1);
  CHECK(noteOn.status_ == (MidiMessage::MIDI_NOTE_ON + 3));
  CHECK(noteOn.data1_ == NOTE_C3);
  CHECK(noteOn.data2_ == 0x64);

  CHECK(timerScope.service.lastMsec == 300);
  REQUIRE(timerScope.service.lastCallback != nullptr);

  ClearQueuedMidiMessagesForTest();
  timerScope.service.lastCallback();

  REQUIRE(GetQueuedMidiMessageCountForTest() == 1);
  const MidiMessage &noteOff = GetQueuedMidiMessageForTest(0);
  CHECK(noteOff.status_ == (MidiMessage::MIDI_NOTE_OFF + 3));
  CHECK(noteOff.data1_ == NOTE_C3);
  CHECK(noteOff.data2_ == 0x00);
}
