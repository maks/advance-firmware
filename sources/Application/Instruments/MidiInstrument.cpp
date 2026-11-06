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

#include "MidiInstrument.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "CommandList.h"
#include "Externals/etl/include/etl/string_stream.h"
#include "Externals/etl/include/etl/to_string.h"
#include "Services/Midi/MidiMessage.h"
#include "System/Console/Trace.h"
#include "config/MemorySections.h"

MidiService *MidiInstrument::svc_ = 0;
TimerService *MidiInstrument::timerSvc_ = 0;
MidiInstrument::NoteOffInfo MidiInstrument::NoteOffInfo::current = {0, 0};

MidiInstrument::MidiInstrument()
    : I_Instrument(&variables_), channel_(FourCC::MidiInstrumentChannel, 0),
      noteLen_(FourCC::MidiInstrumentNoteLength, 0),
      volume_(FourCC::MidiInstrumentVolume, 255),
      table_(FourCC::MidiInstrumentTable, VAR_OFF),
      tableAuto_(FourCC::MidiInstrumentTableAutomation, false),
      program_(FourCC::MidiInstrumentProgram, VAR_OFF) {

  if (svc_ == 0) {
    svc_ = MidiService::GetInstance();
  };

  if (timerSvc_ == 0) {
    timerSvc_ = TimerService::GetInstance();
  };

  // name_ is now an etl::string in the base class, not a Variable
  variables_.insert(variables_.end(), &channel_);
  variables_.insert(variables_.end(), &noteLen_);
  variables_.insert(variables_.end(), &volume_);
  variables_.insert(variables_.end(), &table_);
  variables_.insert(variables_.end(), &tableAuto_);
  variables_.insert(variables_.end(), &program_);

  for (int i = 0; i < SONG_CHANNEL_COUNT; ++i) {
    first_[i] = false;
    lastNoteCount_[i] = 0;
    pendingNoteOffCount_[i] = 0;
    lastNotes_[i].fill(0);
    pendingNoteOffs_[i].fill(0);
    ResetVolumeCommand(i);
  }
}

MidiInstrument::~MidiInstrument(){};

bool MidiInstrument::Init() {
  tableState_.Reset();
  return true;
};

void MidiInstrument::OnStart() {
  tableState_.Reset();

  for (int i = 0; i < SONG_CHANNEL_COUNT; ++i) {
    ResetVolumeCommand(i);
  }

  // Send program change message at the start of playback
  int program = program_.GetInt();

  // Only send program change if a valid program is set
  // 0x80 is used to indicate "OFF"
  if (program != VAR_OFF && program >= 0 && program <= 0x7F) {
    SendProgramChange(channel_.GetInt(), program);
  }

  // Establish the configured MIDI channel volume, including zero.
  SendVolume(EncodeVolume(static_cast<float>(volume_.GetInt())));

  svc_->RegisterActiveChannel(channel_.GetInt());
};

bool MidiInstrument::Start(int c, unsigned char note,
                           InstrumentStartMode mode) {
  const bool hasActiveNotes = lastNoteCount_[c] > 0;
  const bool continueNote =
      mode == InstrumentStartMode::TiedNote && hasActiveNotes;
  const bool sameNoteContinue = continueNote && (lastNotes_[c][0] == note);

  if (sameNoteContinue) {
    return true;
  }

  if (continueNote) {
    pendingNoteOffCount_[c] = lastNoteCount_[c];
    for (int i = 0; i < lastNoteCount_[c]; ++i) {
      pendingNoteOffs_[c][i] = lastNotes_[c][i];
    }
  } else {
    pendingNoteOffCount_[c] = 0;
  }

  first_[c] = true;
  lastNoteCount_[c] = 1;
  lastNotes_[c][0] = note;
  for (int i = 1; i < MAX_MIDI_CHORD_NOTES + 1; ++i) {
    lastNotes_[c][i] = 0;
    pendingNoteOffs_[c][i] = 0;
  }

  Variable *v = FindVariable(FourCC::MidiInstrumentChannel);
  int channel = v->GetInt();

  if (mode != InstrumentStartMode::TiedNote) {
    const bool restoreConfiguredVolume = volumeCommand_[c].overrideActive_;
    ResetVolumeCommand(c);
    if (restoreConfiguredVolume) {
      SendVolume(volumeCommand_[c].lastSent_);
    }

    v = FindVariable(FourCC::MidiInstrumentNoteLength);
    remainingTicks_ = noteLen_.GetInt();
    if (remainingTicks_ > 0) {
      remainingTicks_++;
    } else {
      remainingTicks_ = -1;
    }
  }

  // set initial velocity (changed via InstrumentCommandVelocity)
  velocity_ = INITIAL_NOTE_VELOCITY;
  playing_ = true;
  retrig_ = false;
  pitchBend_ = false;
  useLogCurve_ = false;

  return true;
};

void MidiInstrument::Stop(int c) {

  Trace::Debug("MIDI INSTR STOP!====");

  Variable *v = FindVariable(FourCC::MidiInstrumentChannel);
  int channel = v->GetInt();

  for (int i = 0; i < pendingNoteOffCount_[c]; i++) {
    MidiMessage msg;
    msg.status_ = MidiMessage::MIDI_NOTE_OFF + channel;
    msg.data1_ = pendingNoteOffs_[c][i];
    msg.data2_ = 0x00;
    svc_->QueueMessage(msg);
    Trace::Debug("MIDI pending note OFF[%d]:%d", i, msg.data1_);
  }

  for (int i = 0; i < lastNoteCount_[c]; i++) {
    MidiMessage msg;
    msg.status_ = MidiMessage::MIDI_NOTE_OFF + channel;
    msg.data1_ = lastNotes_[c][i];
    msg.data2_ = 0x00;
    svc_->QueueMessage(msg);
    Trace::Debug("MIDI chord note OFF[%d]:%d", i, msg.data1_);
  }
  // clear last notes array
  lastNotes_[c].fill(0);
  lastNoteCount_[c] = 0;
  pendingNoteOffs_[c].fill(0);
  pendingNoteOffCount_[c] = 0;
  first_[c] = false;
  playing_ = false;
  volumeCommand_[c].ticksRemaining_ = 0;
  volumeCommand_[c].rampActive_ = false;
};

void MidiInstrument::SetChannel(int channel) {
  Variable *v = FindVariable(FourCC::MidiInstrumentChannel);
  v->SetInt(channel);
};

PICOTRACKER_FAST_CODE("midi_instrument_render")
bool MidiInstrument::Render(int channel, AudioSample *buffer, int size,
                            bool updateTick) {
  (void)buffer;
  (void)size;

  // We do it here so we have the opportunity to send some command before
  Variable *v = FindVariable(FourCC::MidiInstrumentChannel);
  int mchannel = v->GetInt();
  if (first_[channel]) {
    // send note
    MidiMessage msg;
    msg.status_ = MidiMessage::MIDI_NOTE_ON + mchannel;
    msg.data1_ = lastNotes_[channel][0];
    msg.data2_ = velocity_;
    svc_->QueueMessage(msg);

    first_[channel] = false;

    // Continued MIDI notes replace the previous note after the new one is
    // armed, so external synth envelopes can keep flowing without a gap.
    for (int i = 0; i < pendingNoteOffCount_[channel]; ++i) {
      msg.status_ = MidiMessage::MIDI_NOTE_OFF + mchannel;
      msg.data1_ = pendingNoteOffs_[channel][i];
      msg.data2_ = 0x00;
      svc_->QueueMessage(msg);
    }
    pendingNoteOffs_[channel].fill(0);
    pendingNoteOffCount_[channel] = 0;
  }

  // Update pitch bend logic if a pitch bend is active.
  if (updateTick) {
    if (pitchBend_) {
      int8_t prev = pitchBendCurrent_;
      if (pitchBendSpeed_ == 0) {
        pitchBendCurrent_ = pitchBendTarget_;
      } else {
        // Calculate the difference and sign for pitch bend direction.
        float diff = pitchBendTarget_ - pitchBendCurrent_;
        float sign = (diff > 0) ? 1.0f : -1.0f;
        float nextValue = pitchBendCurrent_ + (sign * pitchBendStep_);
        if ((sign > 0 && nextValue >= pitchBendTarget_) ||
            (sign < 0 && nextValue <= pitchBendTarget_)) {
          pitchBendCurrent_ = pitchBendTarget_;
          pitchBend_ = false;
          pitchBendStep_ = 1.0f;
        } else {
          if (useLogCurve_) {
            // Exponential pitch bend calculation.
            float diff = pitchBendTarget_ - pitchBendCurrent_;
            pitchBendCurrent_ += diff * interpolationAlpha_;
            // If the pitch is close enough to the target, snap to target and
            // stop bending.
            if (fabs(diff) < PB_MAX_ALPHA) {
              pitchBendCurrent_ = pitchBendTarget_;
              pitchBend_ = false;
            }
          } else {
            // Linear pitch bend calculation.
            pitchBendStep_ = (diff > 0 ? 1 : -1) *
                             (abs(diff) / static_cast<float>(pitchBendSpeed_));
            pitchBendCurrent_ += pitchBendStep_;
          }
        }
      }
      // If pitch bend value changed, send MIDI pitch bend message.
      if (pitchBendCurrent_ != prev) {
        // Convert internal pitch bend value to MIDI pitch bend range (0-16383).
        int16_t midiValue =
            ((pitchBendCurrent_ - PB_7BIT_MAX) * PB_CENTER) / PB_7BIT_MAX;
        int16_t bend = midiValue + PB_CENTER;
        // Clamp bend value to valid MIDI range.
        if (bend < 0) {
          bend = 0;
        } else if (bend > PB_MAX) {
          bend = PB_MAX;
        }
        // Create and queue MIDI pitch bend message.
        MidiMessage msg;
        msg.status_ = MidiMessage::MIDI_PITCH_BEND + mchannel;
        msg.data1_ = bend & 0x7F;
        msg.data2_ = (bend >> 7) & 0x7F;
        svc_->QueueMessage(msg);
      }
    }

    AdvanceVolumeCommand(channel);

    if (remainingTicks_ > 0) {
      remainingTicks_--;
      if (remainingTicks_ == 0) {
        if (!retrig_) {
          Stop(channel);
        } else {
          MidiMessage msg;
          remainingTicks_ = retrigLoop_;
          msg.status_ = MidiMessage::MIDI_NOTE_OFF + mchannel;
          msg.data1_ = lastNotes_[channel][0];
          msg.data2_ = 0x00;
          svc_->QueueMessage(msg);
          msg.status_ = MidiMessage::MIDI_NOTE_ON + mchannel;
          msg.data1_ = lastNotes_[channel][0];
          msg.data2_ = velocity_;
          svc_->QueueMessage(msg);
        };
      };
    };
  }
  return false;
};

bool MidiInstrument::IsInitialized() {
  return true; // Always initialised
};

void MidiInstrument::ProcessCommand(int channel, TrackerCommand cc,
                                    ushort value) {

  Variable *v = FindVariable(FourCC::MidiInstrumentChannel);
  int mchannel = v->GetInt();

  switch (cc) {

  case TrackerCommand::InstrumentCommandRetrigger: {
    unsigned char loop = (value & 0xFF); // number of ticks before repeat
    if (loop != 0) {
      retrig_ = true;
      retrigLoop_ = loop;
      remainingTicks_ = loop;
    } else {
      retrig_ = false;
    }
  } break;

  case TrackerCommand::InstrumentCommandLegato: {
    pitchBendTarget_ = uint8_t(value & 0xFF);
    pitchBendSpeed_ = uint8_t(value >> 8);
    pitchBend_ = true;
    useLogCurve_ = true;

    // Convert to interpolation alpha using a nonlinear curve.
    float growthFactor =
        PB_MIN_GROWTH_FACTOR + (PB_MAX_GROWTH_FACTOR - PB_MIN_GROWTH_FACTOR) *
                                   (1.0f - ((pitchBendSpeed_ - 1) / 253.0f));
    float normalized = (growthFactor - 1.0f) / (PB_MAX_GROWTH_FACTOR - 1.0f);
    interpolationAlpha_ = powf(normalized, PB_CURVE_SHAPE) * PB_MAX_ALPHA;
  } break;

  case TrackerCommand::InstrumentCommandPitchSlide: {
    pitchBendTarget_ = uint8_t(value & 0xFF);
    pitchBendSpeed_ = uint8_t(value >> 8);
    pitchBend_ = true;
    pitchBendStep_ = 1.0f;
    useLogCurve_ = false;
  } break;

  case TrackerCommand::InstrumentCommandVelocity: {
    // VELM cmds set velocity for MIDI steps
    // Ensure velocity doesn't exceed 127 (MIDI spec maximum)
    velocity_ = value & 0x7F;
  }; break;

  case TrackerCommand::InstrumentCommandVolume: {
    StartVolumeCommand(channel, value);
  }; break;

  case TrackerCommand::InstrumentCommandMidiCC: {
    MidiMessage msg;
    msg.status_ = MidiMessage::MIDI_CONTROL_CHANGE + mchannel;
    msg.data1_ = (value & 0x7F00) >> 8;
    msg.data2_ = (value & 0x7F);
    svc_->QueueMessage(msg);
  }; break;

  case TrackerCommand::InstrumentCommandMidiPC: {
    SendProgramChange(mchannel, value & 0x7F);
  }; break;

  case TrackerCommand::InstrumentCommandMidiChord: {
    // split into 4 note offsets (raw semitones, not scale-aware)
    for (int i = 0; i < MAX_MIDI_CHORD_NOTES; i++) {
      uint8_t noteOffset = (value >> (i * 4)) & 0xF;
      if (noteOffset == 0) {
        continue;
      }

      uint8_t rootNote = lastNotes_[channel][0];
      // use raw semitone offset (not scaled to current scale)
      int16_t note = rootNote + noteOffset;

      // clamp to valid MIDI note range (0-127) before queuing or storing
      if (note > 127) {
        continue;
      }

      // save the chord note for sending a note off later
      lastNotes_[channel][i + 1] = static_cast<uint8_t>(note);
      if (lastNoteCount_[channel] < (i + 2)) {
        lastNoteCount_[channel] = i + 2;
      }

      MidiMessage msg;
      msg.status_ = MidiMessage::MIDI_NOTE_ON + mchannel;
      msg.data1_ = static_cast<uint8_t>(note);
      msg.data2_ = velocity_;
      svc_->QueueMessage(msg);
    }
  }; break;
  case TrackerCommand::InstrumentCommandKill: {
    Stop(channel);
  }; break;
  case TrackerCommand::InstrumentCommandGateLength: {
    remainingTicks_ = (value & 0xFF) + 1;
  }; break;
  }
}

uint8_t MidiInstrument::EncodeVolume(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 255.0f) {
    return 127;
  }
  return static_cast<uint8_t>(value) >> 1;
}

void MidiInstrument::SendVolume(uint8_t value) {
  MidiMessage msg;
  msg.status_ = MidiMessage::MIDI_CONTROL_CHANGE + channel_.GetInt();
  msg.data1_ = MidiCC::CC_VOLUME;
  msg.data2_ = value;
  svc_->QueueMessage(msg);
}

void MidiInstrument::ResetVolumeCommand(int channel) {
  VolumeCommandState &state = volumeCommand_[channel];
  state.current_ = static_cast<float>(volume_.GetInt());
  state.target_ = state.current_;
  state.ticksRemaining_ = 0;
  state.lastSent_ = EncodeVolume(state.current_);
  state.overrideActive_ = false;
  state.rampActive_ = false;
}

void MidiInstrument::StartVolumeCommand(int channel, uint16_t value) {
  VolumeCommandState &state = volumeCommand_[channel];
  const uint8_t speed = static_cast<uint8_t>(value >> 8);
  const float target = static_cast<float>(value & 0xFF);

  if (!state.overrideActive_) {
    state.current_ = static_cast<float>(volume_.GetInt());
    state.lastSent_ = EncodeVolume(state.current_);
  }

  state.target_ = target;
  state.ticksRemaining_ = speed;
  state.overrideActive_ = true;
  state.rampActive_ = speed > 0 && state.current_ != state.target_;

  if (!state.rampActive_) {
    state.current_ = state.target_;
    state.ticksRemaining_ = 0;
    state.lastSent_ = EncodeVolume(state.current_);
    SendVolume(state.lastSent_);
  }
}

void MidiInstrument::AdvanceVolumeCommand(int channel) {
  VolumeCommandState &state = volumeCommand_[channel];
  if (!state.rampActive_ || state.ticksRemaining_ == 0) {
    return;
  }

  if (state.ticksRemaining_ == 1) {
    state.current_ = state.target_;
    state.ticksRemaining_ = 0;
    state.rampActive_ = false;
  } else {
    state.current_ += (state.target_ - state.current_) / state.ticksRemaining_;
    state.ticksRemaining_--;
  }

  const uint8_t encoded = EncodeVolume(state.current_);
  if (encoded != state.lastSent_) {
    state.lastSent_ = encoded;
    SendVolume(encoded);
  }
}

etl::string<MAX_INSTRUMENT_NAME_LENGTH> MidiInstrument::GetDefaultName() {
  // use the channel number as a fallback
  Variable *v = FindVariable(FourCC::MidiInstrumentChannel);
  int displayChannelNum = v->GetInt() + 1;
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name;
  etl::string_stream ss(name);
  ss << "MIDI CH " << etl::setfill('0') << etl::setw(2) << displayChannelNum;
  return name;
}

int MidiInstrument::GetTable() {
  Variable *v = FindVariable(FourCC::MidiInstrumentTable);
  return v->GetInt();
};

bool MidiInstrument::GetTableAutomation() {
  Variable *v = FindVariable(FourCC::MidiInstrumentTableAutomation);
  return v->GetBool();
};

void MidiInstrument::GetTableState(TableSaveState &state) {
  memcpy(state.hopCount_, tableState_.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(state.position_, tableState_.position_, sizeof(int) * 3);
  state.groove_ = tableState_.groove_;
};

void MidiInstrument::SetTableState(TableSaveState &state) {
  memcpy(tableState_.hopCount_, state.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(tableState_.position_, state.position_, sizeof(int) * 3);
  tableState_.groove_ = state.groove_;
};

void MidiInstrument::SendProgramChange(int channel, int program) {
  MidiMessage msg;
  msg.status_ = MidiMessage::MIDI_PROGRAM_CHANGE + channel;
  msg.data1_ = program;
  msg.data2_ = MidiMessage::UNUSED_BYTE;
  svc_->QueueMessage(msg);
};

void MidiInstrument::SendProgramChangeWithNote(int channel, int program) {
  // First send the program change
  SendProgramChange(channel, program);

  // Send Note On for C3 with velocity 100 (0x64)
  MidiMessage noteOn;
  noteOn.status_ = MidiMessage::MIDI_NOTE_ON + channel;
  noteOn.data1_ = NOTE_C3;
  noteOn.data2_ = 0x64; // Velocity 100
  svc_->QueueMessage(noteOn);

  // Set up the note-off information for the callback
  NoteOffInfo::current.channel = channel;
  NoteOffInfo::current.note = NOTE_C3;

  // Schedule the note-off message after 300ms using TimerService
  // This is non-blocking and will happen asynchronously
  timerSvc_->TriggerCallback(300, NoteOffCallback);
};

void MidiInstrument::NoteOffCallback() {
  // This static callback will be called after the timer expires
  // Send the Note Off message for the stored note
  if (svc_ != nullptr) {
    MidiMessage noteOff;
    noteOff.status_ = MidiMessage::MIDI_NOTE_OFF + NoteOffInfo::current.channel;
    noteOff.data1_ = NoteOffInfo::current.note;
    noteOff.data2_ = 0x00;
    svc_->SendImmediate(noteOff);
  }
};
