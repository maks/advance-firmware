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

#include "PlayerChannel.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/SyncMaster.h"
#include "PlayerChannelWetDry.h"
#include "System/Profiler/DWTProfiler.h"
#include "config/MemorySections.h"

PlayerChannel::PlayerChannel(int index)
    : index_(index), voice_(index), midiInstrument_(nullptr), muted_(false),
      busIndex_(-1), mixBus_(nullptr) {}

PlayerChannel::~PlayerChannel() {}

void PlayerChannel::StartInstrument(I_Instrument *instr, unsigned char note,
                                    InstrumentStartMode mode,
                                    uint8_t velocity) {
  const bool reuseMidiInstrument =
      (midiInstrument_ == instr) && mode == InstrumentStartMode::TiedNote;
  const bool reuseVoiceInstrument =
      (voice_.GetInstrument() == instr) && mode != InstrumentStartMode::NewNote;

  if ((voice_.GetInstrument() || midiInstrument_) && !reuseMidiInstrument &&
      !reuseVoiceInstrument) {
    StopInstrument();
  }
  if (!instr) {
    return;
  }

  // MIDI instruments are control-only and do not render audio through the
  // shared voice pipeline, but they still need to remain addressable for
  // later commands, note-off, and retriggers on the channel.
  if (instr->GetType() == IT_MIDI) {
    if (instr->Start(index_, note, mode)) {
      midiInstrument_ = instr;
    }
    return;
  }

  voice_.StartInstrument(instr, note, mode, velocity);
};

void PlayerChannel::StopInstrument() {
  if (midiInstrument_) {
    midiInstrument_->Stop(index_);
    midiInstrument_ = nullptr;
  }
  voice_.StopInstrument();
};

void PlayerChannel::ReleaseInstrument() {
  if (midiInstrument_) {
    StopInstrument();
    return;
  }
  voice_.ReleaseInstrument();
};

void PlayerChannel::ReleaseChannel() { ReleaseInstrument(); }

bool PlayerChannel::ProcessCommand(TrackerCommand command, ushort value) {
  if (midiInstrument_) {
    return false;
  }

  I_Instrument *instrument = voice_.GetInstrument();
  // Returning false is deliberate for the legacy SampleInstrument: Player and
  // TablePlayback then fall through to the instrument's locked command path.
  if (!instrument ||
      !InstrumentTypeUsesModernAudioVoice(instrument->GetType())) {
    return false;
  }
  return voice_.ProcessCommand(command, value);
}

PICOTRACKER_FAST_CODE("player_channel_render")
bool PlayerChannel::Render(AudioSample *buffer, int samplecount) {
  const uint32_t profileChannel = static_cast<uint32_t>(index_);
  ScopedDWTProfile channelTotalProfile(
      GetVoiceChannelTotalProfileStats(profileChannel),
      GetVoicesTotalProfileStats());

  if (midiInstrument_) {
    bool tableSlice = SyncMaster::GetInstance()->TableSlice();
    midiInstrument_->Render(index_, buffer, samplecount, tableSlice);
    return false;
  }

  if (!voice_.GetInstrument()) {
    return false;
  }

  bool tableSlice = SyncMaster::GetInstance()->TableSlice();
  bool status = voice_.Render(buffer, samplecount, tableSlice);

  if (!status || muted_) {
    return false;
  }

  const ChannelOutputRouting &routing =
      MixerService::GetInstance()->GetChannelRouting(index_);

  if (routing.channelGain == kAudioSampleZero) {
    return false;
  }

  AudioSample *send0 = routing.sendBuffer[0];
  AudioSample *send1 = routing.sendBuffer[1];
  AudioSample *send2 = routing.sendBuffer[2];
  AudioSample *send3 = routing.sendBuffer[3];
  const AudioSample channelGain = routing.channelGain;
  const AudioSample sendGain0 = routing.sendGain[0];
  const AudioSample sendGain1 = routing.sendGain[1];
  const AudioSample sendGain2 = routing.sendGain[2];
  const AudioSample sendGain3 = routing.sendGain[3];

  const int count = samplecount * 2;
  ScopedDWTProfile outputProfile(
      GetVoiceProfileStats(VoiceProfileComponent::WetDry, profileChannel));

  // FX sends are post-fader aux sends: source already includes the voice
  // render chain, including insert stages, and sendGain already includes the
  // channel gain, so channel volume affects both the dry path and FX sends.
  PlayerChannelWetDry::ApplyWetDryRouting(buffer, count, channelGain, send0,
                                          sendGain0, send1, sendGain1, send2,
                                          sendGain2, send3, sendGain3);

  return true;
};

I_Instrument *PlayerChannel::GetInstrument() {
  if (midiInstrument_) {
    return midiInstrument_;
  }
  return voice_.GetInstrument();
};

void PlayerChannel::SetMute(bool muted) { muted_ = muted; }

void PlayerChannel::SetMixBus(int i) {

  if (i == busIndex_)
    return;

  if (mixBus_) {
    mixBus_->RemoveModule(*this);
  }
  mixBus_ = MixerService::GetInstance()->GetMixBus(i);
  if (mixBus_) {
    mixBus_->AddModule(*this);
    busIndex_ = i;
  } else {
    busIndex_ = -1;
  }
};

void PlayerChannel::Reset() {
  if (midiInstrument_) {
    midiInstrument_->Stop(index_);
    midiInstrument_ = nullptr;
  }
  voice_.Reset();
  if (mixBus_) {
    mixBus_->RemoveModule(*this);
    mixBus_ = nullptr;
  }
  muted_ = false;
  busIndex_ = -1;
};
