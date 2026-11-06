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

#ifndef _MIXER_SERVICE_H_
#define _MIXER_SERVICE_H_

#include "Application/Audio/AudioSample.h"
#include "Application/Commands/ApplicationCommandDispatcher.h" // Would be better done externally and call an API here
#include "Application/Effects/OutputEffectConstants.h"
#include "Application/Effects/OutputEffectSlot.h"
#include "Externals/etl/include/etl/optional.h"
#include "Externals/etl/include/etl/string.h"
#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"
#include "LineInMixer.h"
#include "MixBus.h"
#include "Services/Audio/AudioMixer.h"
#include "Services/Audio/AudioOut.h"
#include "System/Process/SysMutex.h"

enum MixerServiceMode {
  MSM_AUDIO,
  MSM_FILE,
  MSM_FILESPLIT,
};

static constexpr int TRACK_MIX_BUS_COUNT = SONG_CHANNEL_COUNT;
static constexpr int STREAM_MIX_BUS = TRACK_MIX_BUS_COUNT;
static constexpr int OUTPUT_EFFECT_BUS_BASE = STREAM_MIX_BUS + 1;
static constexpr int MAX_BUS_COUNT =
    OUTPUT_EFFECT_BUS_BASE + OUTPUT_EFFECT_COUNT;

struct ChannelOutputRouting {
  AudioSample channelGain;
  AudioSample sendGain[OUTPUT_EFFECT_COUNT];
  AudioSample *sendBuffer[OUTPUT_EFFECT_COUNT];
};

struct EffectReturnRouting {
  AudioSample returnGain;
};

class MixerService : public T_Singleton<MixerService>,
                     public Observable,
                     public I_Observer,
                     public CommandExecuter {

public:
  MixerService();
  virtual ~MixerService();

  bool Init(Project *project);
  void Close();

  bool Start();
  void Stop();

  MixBus *GetMixBus(int i);

  MixBus *GetMasterBus() { return &master_; };

  virtual void Update(Observable &o, I_ObservableData *d);

  void OnPlayerStart(MixerServiceMode mode);
  void OnPlayerStop();
  bool SetRenderTargetOverride(const char *path);
  void ClearRenderTargetOverride();

  void RefreshMixRouting(Project *project);
  void PrepareRenderPass(Project *project);
  int GetPlayedBufferPercentage();

  virtual void Execute(FourCC id, float value);

  AudioOut *GetAudioOut();
  MixerServiceMode GetMode() const { return mode_; }
  const ChannelOutputRouting &GetChannelRouting(int channel) const;
  OutputEffectSlot *GetOutputEffectSlot(int index);
  OutputEffect *GetOutputEffect(int index);
  const OutputEffect *GetOutputEffect(int index) const;
  bool HasOutputEffectTail() const;
  void ClearOutputEffectTails();
  void ClearOutputEffectTailsLocked();

  // Peak level of the live line input added to the master output.
  stereosample GetLineInLevels() const { return lineInMixer_.GetLevels(); }

  void Lock();
  void Unlock();

protected:
  void setRenderingMode(MixerServiceMode mode);

  AudioSample ToLogVolume(int vol, int unityPoint);
  AudioSample GetOutputRenderGain() const;

private:
  bool configureRenderPaths();
  OutputEffectSlot &OutputEffectSlotAt(int index);
  const OutputEffectSlot &OutputEffectSlotAt(int index) const;

  AudioOut *out_;
  MixBus master_;
  LineInMixer lineInMixer_;
  MixBus bus_[MAX_BUS_COUNT];
  etl::optional<OutputEffectSlot> outputEffects_[OUTPUT_EFFECT_COUNT];
  ChannelOutputRouting channelRouting_[SONG_CHANNEL_COUNT];
  EffectReturnRouting effectReturnRouting_[OUTPUT_EFFECT_COUNT];
  SysMutex *sync_;
  Project *project_; // Reference to the current project
  MixerServiceMode mode_ = MSM_AUDIO;
  etl::string<MAX_PROJECT_SAMPLE_PATH_LENGTH> renderTargetOverridePath_;
};
#endif
