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

#ifndef _APPLICATION_MIXER_H_
#define _APPLICATION_MIXER_H_

#include "Application/Audio/AudioFileStreamer.h"
#include "Application/Audio/RecordStreamer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Project.h"
#include "Application/Views/ViewData.h"
#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"
#include "Foundation/Types/Types.h"
#include "PlayerChannel.h"
#include "Services/Audio/AudioOut.h"
#include <cstdint>

class PlayerMixer : public T_Singleton<PlayerMixer>,
                    public Observable,
                    public I_Observer {
public:
  PlayerMixer();
  virtual ~PlayerMixer(){};

  bool Start();
  void Stop();
  bool IsPlaying();
  bool HasTail();
  bool Init(Project *project);
  void BindProject(Project *project);
  void Close();

  void OnPlayerStart(MixerServiceMode msmMode);
  void OnPlayerStop();

  void StartInstrument(int channel, I_Instrument *instrument,
                       unsigned char note, InstrumentStartMode mode,
                       uint8_t velocity = 127);
  void ReleaseChannel(int channel);
  void ReleaseInstrument(int channel);
  void StopInstrument(int channel);
  bool ProcessVoiceCommand(int channel, TrackerCommand command, ushort value);
  void Panic();

  int GetChannelNote(int Channel);

  I_Instrument *GetInstrument(int channel);
  bool GetInstrumentPlaybackPosition(I_Instrument *instrument, int channelCount,
                                     uint32_t &position);

  I_Instrument *GetLastInstrument(int channel);

  void StartChannel(int channel);
  void StopChannel(int channel);

  inline bool IsChannelPlaying(int channel) const {
    return isChannelPlaying_[channel];
  }

  void StartStreaming(const char *name, int startSample = 0);
  void StartLoopingStreaming(const char *name);
  void StopStreaming();

  // Returns true if the file streamer (preview) is active.
  bool IsStreamingActive();

  void StartRecordStreaming(uint16_t *srcBuffer, uint32_t size, bool stereo);
  void StopRecordStreaming();

  stereosample GetMasterOutLevel();

  void Update(Observable &o, I_ObservableData *d);
  int GetPlayedBufferPercentage();

  void SetChannelMute(int channel, bool mute);
  inline bool IsChannelMuted(int channel) const {
    return channel_[channel]->IsMuted();
  }

  const char *GetPlayedNote(int channel);
  const char *GetPlayedOctive(int channel);
  bool GetPlayedSliceIndex(int channel, uint8_t &sliceIndex);

  AudioOut *GetAudioOut();

  void Lock();
  void Unlock();

  // Get the current project
  Project *GetProject() { return project_; }

  etl::array<stereosample, SONG_CHANNEL_COUNT> *GetMixerLevels();

private:
  Project *project_;
  etl::array<stereosample, SONG_CHANNEL_COUNT> mixerLevels_;

  I_Instrument *lastInstrument_[SONG_CHANNEL_COUNT];
  bool isChannelPlaying_[SONG_CHANNEL_COUNT];

  AudioFileStreamer fileStreamer_;
  RecordStreamer recordStreamer_;
  PlayerChannel *channel_[SONG_CHANNEL_COUNT];

  // store trigger notes, 0xFF = none

  unsigned char notes_[SONG_CHANNEL_COUNT];
};

#endif
