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

#include "PlayerMixer.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Utils/char.h"
#include "Services/Midi/MidiService.h"
#include "SyncMaster.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "config/MemorySections.h"
#include <cstdint>
#include <math.h>
#include <stdlib.h>

PlayerMixer::PlayerMixer() {

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastInstrument_[i] = 0;
    isChannelPlaying_[i] = false;
    notes_[i] = 0xFF;
  };

  alignas(PlayerChannel) static char
      playerChannelMemBuf[sizeof(PlayerChannel) * SONG_CHANNEL_COUNT];
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    channel_[i] =
        new (playerChannelMemBuf + i * sizeof(PlayerChannel)) PlayerChannel(i);
  }
}

bool PlayerMixer::Init(Project *project) {

  MixerService *ms = MixerService::GetInstance();
  if (!ms->Init(project)) {
    return false;
  }

  AudioMixer *audioMixer = ms->GetMixBus(STREAM_MIX_BUS);
  audioMixer->AddModule(fileStreamer_);

  project_ = project;

  // Add the record mixer
  audioMixer->AddModule(recordStreamer_);

  // Init states
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastInstrument_[i] = 0;
  };

  // Setup mixbus
  Mixer *mixer = Mixer::GetInstance();
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    channel_[i]->SetMixBus(mixer->GetBus(i));
  }

  // streamer need access to project to get current volume
  fileStreamer_.SetProject(project);

  return true;
};

void PlayerMixer::BindProject(Project *project) {
  project_ = project;
  fileStreamer_.SetProject(project);
  MixerService::GetInstance()->RefreshMixRouting(project);

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastInstrument_[i] = 0;
    isChannelPlaying_[i] = false;
    notes_[i] = 0xFF;
  }
}

void PlayerMixer::Close() {

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    channel_[i]->Reset();
  }

  MixerService *ms = MixerService::GetInstance();
  ms->Close();
}

bool PlayerMixer::Start() {
  MixerService *ms = MixerService::GetInstance();
  ms->AddObserver(*this);

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    notes_[i] = NO_NOTE;
  };

  return ms->Start();
};

void PlayerMixer::Stop() {
  MixerService *ms = MixerService::GetInstance();
  ms->Stop();
  ms->RemoveObserver(*this);
};

void PlayerMixer::StartChannel(int channel) {
  isChannelPlaying_[channel] = true;
};

void PlayerMixer::StopChannel(int channel) {

  StopInstrument(channel);
  isChannelPlaying_[channel] = false;
};

I_Instrument *PlayerMixer::GetLastInstrument(int channel) {
  return lastInstrument_[channel];
};

stereosample PlayerMixer::GetMasterOutLevel() {
  MixerService *ms = MixerService::GetInstance();
  return ms->GetMasterBus()->GetMixerLevels();
}

etl::array<stereosample, SONG_CHANNEL_COUNT> *PlayerMixer::GetMixerLevels() {
  MixerService *ms = MixerService::GetInstance();

  // Get the current mixer levels from each bus
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    AudioMixer *audioMixer = ms->GetMixBus(i);
    mixerLevels_[i] = audioMixer->GetMixerLevels();
  }

  return &mixerLevels_;
}

PICOTRACKER_FAST_CODE("player_mixer_update")
void PlayerMixer::Update(Observable &o, I_ObservableData *d) {

  // Notifies the player so that pattern data is processed

  SetChanged();
  NotifyObservers();

  MixerService *ms = MixerService::GetInstance();
  ms->PrepareRenderPass(project_);
};

void PlayerMixer::StartInstrument(int channel, I_Instrument *instrument,
                                  unsigned char note,
                                  InstrumentStartMode mode,
                                  uint8_t velocity) {
  channel_[channel]->StartInstrument(instrument, note, mode, velocity);
  lastInstrument_[channel] = instrument;
  notes_[channel] = note;
};

void PlayerMixer::ReleaseChannel(int channel) {
  channel_[channel]->ReleaseChannel();
  isChannelPlaying_[channel] = false;
  notes_[channel] = NO_NOTE;
}

void PlayerMixer::StopInstrument(int channel) {
  channel_[channel]->StopInstrument();
  notes_[channel] = NO_NOTE;
}

void PlayerMixer::ReleaseInstrument(int channel) {
  channel_[channel]->ReleaseInstrument();
}

bool PlayerMixer::ProcessVoiceCommand(int channel, TrackerCommand command,
                                      ushort value) {
  return channel_[channel]->ProcessCommand(command, value);
}

I_Instrument *PlayerMixer::GetInstrument(int channel) {
  return channel_[channel]->GetInstrument();
}

bool PlayerMixer::GetInstrumentPlaybackPosition(I_Instrument *instrument,
                                                int channelCount,
                                                uint32_t &position) {
  position = 0;
  if (!instrument) {
    return false;
  }

  if (channelCount < 0) {
    channelCount = 0;
  }
  if (channelCount > SONG_CHANNEL_COUNT) {
    channelCount = SONG_CHANNEL_COUNT;
  }

  for (int i = 0; i < channelCount; ++i) {
    if (channel_[i]->GetInstrument() == instrument &&
        instrument->GetPlaybackPosition(i, position)) {
      return true;
    }
  }
  return false;
}

void PlayerMixer::Panic() {
  for (int i = 0; i < SONG_CHANNEL_COUNT; ++i) {
    StopChannel(i);
  }
  MixerService::GetInstance()->ClearOutputEffectTailsLocked();
}

int PlayerMixer::GetPlayedBufferPercentage() {
  MixerService *ms = MixerService::GetInstance();
  return ms->GetPlayedBufferPercentage();
};

void PlayerMixer::SetChannelMute(int channel, bool mode) {
  channel_[channel]->SetMute(mode);
}

void PlayerMixer::StartStreaming(const char *name, int startSample) {
  fileStreamer_.Start(name, startSample);
};

void PlayerMixer::StartLoopingStreaming(const char *name) {
  fileStreamer_.Start(name, 0, true);
};

void PlayerMixer::StopStreaming() { fileStreamer_.Stop(); };

bool PlayerMixer::IsStreamingActive() { return fileStreamer_.IsPlaying(); };

void PlayerMixer::StartRecordStreaming(uint16_t *srcBuffer, uint32_t size,
                                       bool stereo) {
  recordStreamer_.Start(srcBuffer, size, stereo);
};

void PlayerMixer::StopRecordStreaming() { recordStreamer_.Stop(); };

bool PlayerMixer::IsPlaying() {
  if (fileStreamer_.IsPlaying() || recordStreamer_.IsPlaying()) {
    return true;
  }

  for (int i = 0; i < SONG_CHANNEL_COUNT; ++i) {
    if (isChannelPlaying_[i] || channel_[i]->GetInstrument() != nullptr) {
      return true;
    }
  }

  return false;
}

bool PlayerMixer::HasTail() {
  // File and record streamers are deliberately excluded: they represent
  // preview or monitoring states, not sound released by the sequencer.
  for (int i = 0; i < SONG_CHANNEL_COUNT; ++i) {
    if (channel_[i]->GetInstrument() != nullptr) {
      return true;
    }
  }

  MixerService *mixerService = MixerService::GetInstance();
  return mixerService != nullptr && mixerService->HasOutputEffectTail();
}

void PlayerMixer::OnPlayerStart(MixerServiceMode msmMode) {
  MixerService *ms = MixerService::GetInstance();
  ms->OnPlayerStart(msmMode);
}

void PlayerMixer::OnPlayerStop() {
  MixerService *ms = MixerService::GetInstance();
  ms->OnPlayerStop();
}

static char noteBuffer[5];

static bool shouldShowSlice(int channel, uint8_t &sliceIndex,
                            I_Instrument *instrument,
                            const unsigned char *notes) {
  if (!instrument) {
    return false;
  }
  if (notes[channel] == 0xFF) {
    return false;
  }

  if (instrument->GetType() == IT_SAMPLE) {
    SampleInstrument *sampleInstr = static_cast<SampleInstrument *>(instrument);
    if (!sampleInstr->ShouldDisplaySliceForNote(notes[channel])) {
      return false;
    }
    sliceIndex =
        static_cast<uint8_t>(notes[channel] - SampleInstrument::SliceNoteBase);
    return true;
  }

  if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    SampleSourceInstrument *sampleSourceInstr =
        static_cast<SampleSourceInstrument *>(instrument);
    if (!sampleSourceInstr->ShouldDisplaySliceForNote(notes[channel])) {
      return false;
    }
    sliceIndex = static_cast<uint8_t>(notes[channel] -
                                      SampleSourceInstrument::SliceNoteBase);
    return true;
  }

  return false;
}

int PlayerMixer::GetChannelNote(int channel) { return notes_[channel]; }

const char *PlayerMixer::GetPlayedNote(int channel) {

  if (notes_[channel] <= HIGHEST_NOTE) {
    note2visualizer(notes_[channel], noteBuffer);
    return noteBuffer;
  }
  return "  ";
};

const char *PlayerMixer::GetPlayedOctive(int channel) {
  if (notes_[channel] <= HIGHEST_NOTE) {
    if (!IsChannelMuted(channel)) {
      oct2visualizer(notes_[channel], noteBuffer);
      return noteBuffer;
    } else {
      return "--";
    }
  }
  return "  ";
};

bool PlayerMixer::GetPlayedSliceIndex(int channel, uint8_t &sliceIndex) {
  return shouldShowSlice(channel, sliceIndex, lastInstrument_[channel], notes_);
}

AudioOut *PlayerMixer::GetAudioOut() {
  MixerService *ms = MixerService::GetInstance();
  return ms->GetAudioOut();
};

void PlayerMixer::Lock() {
  MixerService *ms = MixerService::GetInstance();
  ms->Lock();
};

void PlayerMixer::Unlock() {
  MixerService *ms = MixerService::GetInstance();
  ms->Unlock();
};
