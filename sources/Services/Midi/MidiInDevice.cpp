/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "MidiInDevice.h"
#include "Application/Model/Song.h"
#include "Application/Player/Player.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"

#define MIDI_CHANNEL_MASK 0x0F
#define MIDI_DATA_MASK 0x7F

// Set this to true to log MIDI events to stdout for debugging
bool MidiInDevice::dumpEvents_ = false;

MidiInDevice::MidiInDevice(const char *name)
    : Observable(&observers_), isRunning_(false) {
  for (int channel = 0; channel < 16; channel++) {
    // MIDI program data is zero-based, so program 00 is user-facing program 01.
    channelPrograms_[channel] = 0;
  }
};

MidiInDevice::~MidiInDevice(){};

bool MidiInDevice::Init() { return initDriver(); };

void MidiInDevice::Close() { closeDriver(); };

bool MidiInDevice::Start() {
  isRunning_ = true;
  resetParserState();

  return startDriver();
};

void MidiInDevice::Stop() {
  isRunning_ = false;
  stopDriver();
  resetParserState();
  releaseActiveNotes();
};

bool MidiInDevice::IsRunning() { return isRunning_; };

void MidiInDevice::resetParserState() {
  midiStatus = 0;
  midiDataCount = 0;
}

void MidiInDevice::releaseActiveNotes() {
  Player *player = Player::GetExistingInstance();
  if (player != nullptr) {
    player->ReleaseMidiInputSource(getInputSource());
  }
}

void MidiInDevice::onMidiStart() {
  MidiSyncData data(MSM_START);
  SetChanged();
  NotifyObservers();

  // Get the Player instance and start playback similar to SongView's onStart
  Player *player = Player::GetInstance();
  if (player) {
    // Start playback on all channels (0-7) like the play button does
    player->OnSongStartButton(0, 7, false, false);
  }
};

void MidiInDevice::onMidiStop() {
  MidiSyncData data(MSM_STOP);
  SetChanged();
  NotifyObservers();

  // Get the Player instance and stop playback
  Player *player = Player::GetInstance();
  if (player) {
    // Only stop if the player is currently running
    if (player->IsRunning()) {
      // Use the direct Stop() method instead of OnSongStartButton
      // to avoid toggling behavior
      player->Stop();
    }
  }
};

void MidiInDevice::onMidiContinue() {
  MidiSyncData data(MSM_CONTINUE);
  SetChanged();
  NotifyObservers();

  // Get the Player instance and start playback
  Player *player = Player::GetInstance();
  if (player) {
    // Only start if the player is not already running
    if (!player->IsRunning()) {
      // Start playback on all channels (0-7) like the play button does
      player->OnSongStartButton(0, 7, false, false);
    }
  }
};

void MidiInDevice::onMidiTempoTick() {
  MidiSyncData data(MSM_TEMPOTICK);
  SetChanged();
  NotifyObservers();
};

void MidiInDevice::queueEvent(MidiEvent &event){
    // TODO: queue the event
};

void MidiInDevice::onDriverMessage(MidiMessage &message) {
  SetChanged();
  NotifyObservers(&message);
  if (isRunning_ && !message.IsConsumed()) {
    treatChannelEvent(message);
  }
};

void MidiInDevice::Trigger(){
    // No-op: events are handled immediately in onDriverMessage.
};

void MidiInDevice::treatChannelEvent(MidiMessage &event) {

  int midiChannel = event.status_ & MIDI_CHANNEL_MASK;

  bool isMidiClockEvent = (event.status_ == MidiMessage::MIDI_CLOCK);

  // display as hex
  if (!isMidiClockEvent) {
    Trace::Debug("midi:%02X:%02X:%02X", event.status_, event.data1_,
                 event.data2_);
    Trace::Debug("miditype:%02X", event.GetType());
  }

  // First check for system real-time messages which need to be compared with
  // the full status byte
  if (event.status_ == MidiMessage::MIDI_CLOCK) {
    onMidiTempoTick();
    return;
  } else if (event.status_ == MidiMessage::MIDI_START) {
    onMidiStart();
    return;
  } else if (event.status_ == MidiMessage::MIDI_STOP) {
    onMidiStop();
    return;
  } else if (event.status_ == MidiMessage::MIDI_CONTINUE) {
    onMidiContinue();
    return;
  }

  // Process channel messages using GetType()
  switch (event.GetType()) {
  case MidiMessage::MIDI_NOTE_OFF: {
    const uint8_t note = event.data1_ & MIDI_DATA_MASK;

    Player *player = Player::GetInstance();
    if (player) {
      player->StopMidiNote(getInputSource(), static_cast<uint8_t>(midiChannel),
                           note);
    }
  } break;

  case MidiMessage::MIDI_NOTE_ON: {
    int note = event.data1_ & MIDI_DATA_MASK;
    int value = event.data2_ & MIDI_DATA_MASK;

    const int instrumentIndex = GetProgramForChannel(midiChannel);

    Player *player = Player::GetInstance();
    if (player) {
      // If velocity is 0, it's actually a note off in MIDI
      if (value == 0) {
        // Handle as note off - only stop if this note is active on this channel
        player->StopMidiNote(getInputSource(),
                             static_cast<uint8_t>(midiChannel),
                             static_cast<uint8_t>(note));
      } else if (instrumentIndex >= 0 &&
                 instrumentIndex < MAX_INSTRUMENT_COUNT) {
        // Get the next available reserved song channel for this note
        Trace::Debug("Playing note %d on MIDI channel %d (instrument %d)", note,
                     midiChannel, instrumentIndex);
        player->PlayMidiNote(
            getInputSource(), static_cast<uint8_t>(midiChannel),
            static_cast<uint8_t>(instrumentIndex), static_cast<uint8_t>(note),
            static_cast<uint8_t>(value));
      }
    }
  } break;

  case MidiMessage::MIDI_AFTERTOUCH: {
    int note = event.data1_ & MIDI_DATA_MASK;
    int data = event.data2_ & MIDI_DATA_MASK;

    // TODO: handle aftertouch
  } break;

  case MidiMessage::MIDI_CONTROL_CHANGE: {
    int cc = event.data1_ & MIDI_DATA_MASK;
    int data = event.data2_ & MIDI_DATA_MASK;

    if (dumpEvents_) {
      Trace::Log("EVENT", "midi:cc:%d:%d", cc, data);
    }
    Player *player = Player::GetInstance();
    if (player) {
      player->ApplyMidiChannelCC(
          getInputSource(), static_cast<uint8_t>(midiChannel),
          static_cast<uint8_t>(cc), static_cast<uint8_t>(data));
      player->ApplyMidiVoiceCC(
          getInputSource(), static_cast<uint8_t>(midiChannel),
          static_cast<uint8_t>(GetProgramForChannel(midiChannel)),
          static_cast<uint8_t>(cc), static_cast<uint8_t>(data));
    }
  } break;

  case MidiMessage::MIDI_PROGRAM_CHANGE: {
    const uint8_t data = event.data1_ & MIDI_DATA_MASK;

    SetProgramForChannel(midiChannel, data);
  } break;

  case MidiMessage::MIDI_CHANNEL_AFTERTOUCH: {
    int data = event.data1_ & MIDI_DATA_MASK;

    // TODO: handle channel aftertouch
  } break;

  case MidiMessage::MIDI_PITCH_BEND: {
    // TODO: handle pitch bend
  } break;

  // Handle any other MIDI message types
  case MidiMessage::MIDI_CLOCK:
  case MidiMessage::MIDI_START:
  case MidiMessage::MIDI_CONTINUE:
  case MidiMessage::MIDI_STOP:
  case MidiMessage::MIDI_ACTIVE_SENSING:
  case MidiMessage::MIDI_SYSTEM_RESET:
  default:
    break;
  };
};

// MIDI program selection uses the fixed program-to-instrument identity mapping.
void MidiInDevice::SetProgramForChannel(int midiChannel, uint8_t program) {
  if (midiChannel >= 0 && midiChannel < 16) {
    if (channelPrograms_[midiChannel] != program) {
      Player *player = Player::GetExistingInstance();
      if (player) {
        player->ResetMidiPickups(getInputSource(),
                                 static_cast<uint8_t>(midiChannel));
      }
    }
    channelPrograms_[midiChannel] = program;
    // Trace::Log("MIDI", "Selected program %d on MIDI channel %d", program,
    //            midiChannel);
  }
}

int MidiInDevice::GetProgramForChannel(int midiChannel) const {
  if (midiChannel >= 0 && midiChannel < 16) {
    return channelPrograms_[midiChannel];
  }
  return -1; // No program assigned
}

void MidiInDevice::processMidiData(uint8_t data) {
  // Handle MIDI data byte
  if (data & 0x80) {
    // This is a status byte

    // System Real-Time messages can appear anywhere and have no data bytes
    if (data >= 0xF8) {
      MidiMessage msg;
      msg.status_ = data;
      msg.data1_ = MidiMessage::UNUSED_BYTE;
      msg.data2_ = MidiMessage::UNUSED_BYTE;
      onDriverMessage(msg);
      return; // Don't change the running status
    }

    // For all other status bytes, reset the data count
    midiStatus = data;
    midiDataCount = 0;

    // Determine how many data bytes to expect based solely on the status byte
    if (data >= 0xF0) {
      // System Common messages
      switch (data) {
      case 0xF1: // MIDI Time Code Quarter Frame
      case 0xF3: // Song Select
        midiDataBytes = 1;
        break;
      case 0xF2: // Song Position Pointer
        midiDataBytes = 2;
        break;
      case 0xF0: // Start of System Exclusive
        // SysEx messages are variable length and end with 0xF7
        // For simplicity, we're not fully handling SysEx here
        midiDataBytes = 0; // Special case, handled differently
        break;
      default:
        // All other System Common messages have no data bytes
        midiDataBytes = 0;

        // For messages with no data bytes, send them immediately
        MidiMessage msg;
        msg.status_ = midiStatus;
        msg.data1_ = MidiMessage::UNUSED_BYTE;
        msg.data2_ = MidiMessage::UNUSED_BYTE;
        onDriverMessage(msg);
        break;
      }
    } else {
      // Channel messages - determine bytes by status byte range
      uint8_t msgType = data & 0xF0;

      // Program Change and Channel Pressure have 1 data byte
      if (msgType == 0xC0 || msgType == 0xD0) {
        midiDataBytes = 1;
      } else {
        // All other channel messages have 2 data bytes
        midiDataBytes = 2;
      }
    }
  } else {
    // This is a data byte
    if (midiStatus == 0) {
      // Ignore data bytes without status
      Trace::Debug("MIDI", "Ignored data byte without status: 0x%02X", data);
      return;
    }

    if (midiDataCount == 0) {
      midiData1 = data;
      midiDataCount++;

      // If we only expect one data byte, we have a complete message
      if (midiDataBytes == 1) {
        MidiMessage msg;
        msg.status_ = midiStatus;
        msg.data1_ = midiData1;
        msg.data2_ = MidiMessage::UNUSED_BYTE;
        onDriverMessage(msg);
      }
    } else if (midiDataCount == 1 && midiDataBytes == 2) {
      // We have all the data we need for a 2-byte message
      MidiMessage msg;
      msg.status_ = midiStatus;
      msg.data1_ = midiData1;
      msg.data2_ = data;
      onDriverMessage(msg);

      // Reset data count but keep status for running status
      midiDataCount = 0;
    }
  }
}
