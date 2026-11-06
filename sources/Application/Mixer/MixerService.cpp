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

#include "MixerService.h"
#include "Application/AppWindow.h"
#include "Application/Application.h"
#include "Application/Effects/OutputEffectDefinitions.h"
#include "Application/Model/Config.h"
#include "Application/Model/Project.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Player/Player.h"
#include "Application/Player/PlayerMixer.h"
#include "Application/Utils/char.h"
#include "Application/Views/BaseClasses/BrowserPathUtils.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/AudioOut.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/Profiler/DWTProfiler.h"
#include "System/System/System.h"
#include "config/MemorySections.h"
#include "platform.h"
#include <algorithm>
#include <cmath>
#include <nanoprintf.h>

namespace {

__attribute__((aligned(32))) __attribute__((section(".DTCMRAM")))
AudioSample gOutputEffectWetBuffers[OUTPUT_EFFECT_COUNT][MAX_SAMPLE_COUNT * 2];

constexpr uint16_t kProfileOrderOutputFx1 = 60;
constexpr uint16_t kProfileOrderOutputFx2 = 61;
constexpr uint16_t kProfileOrderOutputFx3 = 62;
constexpr uint16_t kProfileOrderOutputFx4 = 63;
constexpr uint16_t kProfileOrderOutputFxTotal = 64;
constexpr uint32_t kOutputEffectsBudgetPctX1000 = 40000U;
constexpr uint32_t kDelayBudgetPctX1000 = 10000U;
constexpr uint32_t kReverbBudgetPctX1000 = 30000U;

DWTProfileStats gOutputEffectProfiles[OUTPUT_EFFECT_COUNT] = {
    {.name_ = "ofx1"},
    {.name_ = "ofx2"},
    {.name_ = "delay", .budgetPctX1000_ = kDelayBudgetPctX1000},
    {.name_ = "reverb", .budgetPctX1000_ = kReverbBudgetPctX1000},
};
DWTProfileStats gOutputEffectTotalProfile = {
    .name_ = "fx total",
    .budgetPctX1000_ = kOutputEffectsBudgetPctX1000,
};

void registerOutputEffectProfiles() {
  static bool registered = false;
  if (registered) {
    return;
  }

  gOutputEffectProfiles[0].displayOrder_ = kProfileOrderOutputFx1;
  gOutputEffectProfiles[1].displayOrder_ = kProfileOrderOutputFx2;
  gOutputEffectProfiles[2].displayOrder_ = kProfileOrderOutputFx3;
  gOutputEffectProfiles[3].displayOrder_ = kProfileOrderOutputFx4;
  gOutputEffectTotalProfile.displayOrder_ = kProfileOrderOutputFxTotal;
  gOutputEffectTotalProfile.aggregateChildren_[0] = &gOutputEffectProfiles[0];
  gOutputEffectTotalProfile.aggregateChildren_[1] = &gOutputEffectProfiles[1];
  gOutputEffectTotalProfile.aggregateChildren_[2] = &gOutputEffectProfiles[2];
  gOutputEffectTotalProfile.aggregateChildren_[3] = &gOutputEffectProfiles[3];
  gOutputEffectTotalProfile.aggregateChildCount_ = OUTPUT_EFFECT_COUNT;

  for (int i = 0; i < OUTPUT_EFFECT_COUNT; ++i) {
    RegisterDWTProfileStats(&gOutputEffectProfiles[i]);
  }
  RegisterDWTProfileStats(&gOutputEffectTotalProfile);
  registered = true;
}

} // namespace

MixerService::MixerService() : master_(), sync_(platform_mutex()) {
  registerOutputEffectProfiles();
  master_.SetProfileMixMath(true);
  OutputEffectSlot::SetMixProfileStats(AudioMixer::GetMixProfileStats());
  out_ = 0;
  project_ = NULL;
  mode_ = MSM_AUDIO;
  master_.SetName("Master");

  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    channelRouting_[channel].channelGain = 0;
    for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
      channelRouting_[channel].sendGain[fx] = 0;
      channelRouting_[channel].sendBuffer[fx] = nullptr;
    }
  }

  for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
    effectReturnRouting_[fx].returnGain = kAudioSampleOne;
  }
};

MixerService::~MixerService(){};

bool MixerService::Init(Project *project) {
  NAssert(project);
  if (project == nullptr) {
    return false;
  }

  for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
    OutputEffect *effect = project->GetOutputEffect(fx);
    if (effect == nullptr) {
      Trace::Error("[MixerService::Init] Missing fixed output effect %d", fx);
      return false;
    }
    outputEffects_[fx].emplace(gOutputEffectWetBuffers[fx], *effect);
  }

  // Send destinations are fixed for the lifetime of the mixer. Keeping them
  // valid at zero gain gives every rendered channel the same four-send path.
  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
      channelRouting_[channel].sendBuffer[fx] =
          OutputEffectSlotAt(fx).GetAccumulationBuffer();
    }
  }

  out_ = 0;
  // Create the output depending on rendering mode
  Audio *audio = Audio::GetInstance();
  out_ = audio->GetFirstOutput();

  bool result = false;

  char buffer[5];
  // Master renders modules in insertion order. Track and stream buses must
  // fill all direct sends first, followed by FX1, FX2, delay, and reverb so
  // each effect can add its wet output to later effects in the same pass.
  for (int i = 0; i < MAX_BUS_COUNT; i++) {
    if (i < TRACK_MIX_BUS_COUNT) {
      hex2char(i, buffer);
      bus_[i].SetName(etl::string<12>(buffer));
    } else if (i == STREAM_MIX_BUS) {
      bus_[i].SetName("Stream");
    } else {
      int fxIndex = i - OUTPUT_EFFECT_BUS_BASE;
      NAssert(fxIndex >= 0 && fxIndex < OUTPUT_EFFECT_COUNT);
      npf_snprintf(buffer, sizeof(buffer), "F%d", fxIndex + 1);
      bus_[i].SetName(etl::string<12>(buffer));
      bus_[i].SetRenderProfileStats(&gOutputEffectProfiles[fxIndex]);
      bus_[i].AddModule(OutputEffectSlotAt(fxIndex));
    }
    master_.AddModule(bus_[i]);
    master_.SetName("Master");
  }

  if (out_) {
    result = out_->Init();
    if (result) {
      out_->AddModule(master_);
      // The line input joins the mix after the master bus so it never feeds
      // the channel effect sends, and before the render gain so a mixdown
      // captures exactly what is monitored.
      lineInMixer_.SetSource(audio->GetLineInSource());
      out_->AddModule(lineInMixer_);
    }

    out_->AddObserver(*MidiService::GetInstance());
    bool pathConfigured = configureRenderPaths();
    if (!pathConfigured) {
      Trace::Error("[MixerService::Init] Failed to set audio render paths");
      return false;
    }
  }

  if (result) {
    Trace::Debug("[MixerService::Init] Out initialized");
  } else {
    Trace::Error("[MixerService::Init] Failed to get output");
  }
  return (result);
};

void MixerService::Close() {
  if (out_) {
    out_->RemoveObserver(*MidiService::GetInstance());
    out_->Close();
    out_->ClearModules();
    master_.ClearModules();
  }
  lineInMixer_.SetGain(kAudioSampleZero);
  lineInMixer_.SetSource(nullptr);
  for (int i = 0; i < MAX_BUS_COUNT; i++) {
    bus_[i].ClearModules();
  }
  out_ = 0;
};

bool MixerService::Start() {
  MidiService::GetInstance()->Start();
  if (out_) {
    out_->AddObserver(*this);
    out_->Start();
  }
  return true;
};

void MixerService::Stop() {
  MidiService::GetInstance()->Stop();
  if (out_) {
    out_->Stop();
    out_->RemoveObserver(*this);
  }
}

MixBus *MixerService::GetMixBus(int i) { return &(bus_[i]); };

const ChannelOutputRouting &MixerService::GetChannelRouting(int channel) const {
  NAssert(channel >= 0 && channel < SONG_CHANNEL_COUNT);
  return channelRouting_[channel];
}

OutputEffectSlot *MixerService::GetOutputEffectSlot(int index) {
  if (index < 0 || index >= OUTPUT_EFFECT_COUNT ||
      !outputEffects_[index].has_value()) {
    return nullptr;
  }
  return &outputEffects_[index].value();
}

OutputEffect *MixerService::GetOutputEffect(int index) {
  OutputEffectSlot *slot = GetOutputEffectSlot(index);
  if (slot == nullptr) {
    return nullptr;
  }
  return &slot->GetEffect();
}

const OutputEffect *MixerService::GetOutputEffect(int index) const {
  if (index < 0 || index >= OUTPUT_EFFECT_COUNT ||
      !outputEffects_[index].has_value()) {
    return nullptr;
  }
  return &outputEffects_[index].value().GetEffect();
}

bool MixerService::HasOutputEffectTail() const {
  for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
    if (outputEffects_[fx].has_value() &&
        outputEffects_[fx].value().IsTailActive()) {
      return true;
    }
  }
  return false;
}

void MixerService::ClearOutputEffectTails() {
  Lock();
  ClearOutputEffectTailsLocked();
  Unlock();
}

void MixerService::ClearOutputEffectTailsLocked() {
  for (int i = 0; i < OUTPUT_EFFECT_COUNT; ++i) {
    if (outputEffects_[i].has_value()) {
      outputEffects_[i].value().ResetState();
    }
  }
}

PICOTRACKER_FAST_CODE("mixer_service_update")
void MixerService::Update(Observable &o, I_ObservableData *d) {

  AudioDriver::Event *event = (AudioDriver::Event *)d;
  if (event->type_ == AudioDriver::Event::ADET_BUFFERNEEDED) {
    Lock();
    // The sequencer can stop rendering during NotifyObservers(), resetting
    // mode_. Keep the final stem buffer silent even after that transition.
    const bool wasRenderingStems = mode_ == MSM_FILESPLIT;
    SetChanged();
    NotifyObservers();

    out_->Trigger(wasRenderingStems || mode_ == MSM_FILESPLIT);
    Unlock();
  }
}

AudioSample MixerService::ToLogVolume(int vol, int unityPoint) {
  if (vol < 0)
    vol = 0;
  if (vol >= unityPoint) {
    return kAudioSampleOne;
  }

  float normalizedVol = float(vol) / float(unityPoint);
  return normalizedVol * normalizedVol;
}

AudioSample MixerService::GetOutputRenderGain() const {
  Audio *audio = Audio::GetInstance();
  if (audio == nullptr) {
    return kAudioSampleOne;
  }

  int volume = audio->GetMixerVolume();
  if (volume < 0) {
    volume = 0;
  } else if (volume > 100) {
    volume = 100;
  }

  // The codec output volume uses 0.5 dB steps from -50.5 dB to -0.5 dB.
  const float attenuationDb = -50.5f + (static_cast<float>(volume) * 0.5f);
  const float linearGain = std::pow(10.0f, attenuationDb / 20.0f);
  return std::min(linearGain * 2.0f, kAudioSampleOne);
}

void MixerService::RefreshMixRouting(Project *project) {
  NAssert(project);
  project_ = project;

  master_.SetVolume(
      ToLogVolume(project->GetMasterVolume(), Project::MixerVolumeMaximum));

  // The line input is mixed after the master bus, so its own level is the only
  // gain it passes through. Zero releases the converter.
  lineInMixer_.SetGain(
      ToLogVolume(project->GetLineInMixLevel(), Project::MixerVolumeMaximum));

  for (int i = 0; i < TRACK_MIX_BUS_COUNT; ++i) {
    bus_[i].SetVolume(kAudioSampleOne);
    channelRouting_[i].channelGain =
        ToLogVolume(project->GetChannelVolume(i), Project::MixerVolumeMaximum);

    for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
      AudioSample sendMix = ToLogVolume(project->GetChannelFXSend(i, fx),
                                        OutputEffectSendMaximum);
      AudioSample sendGain = channelRouting_[i].channelGain * sendMix;
      channelRouting_[i].sendGain[fx] = sendGain;
    }
  }

  bus_[STREAM_MIX_BUS].SetVolume(kAudioSampleOne);

  for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
    bus_[OUTPUT_EFFECT_BUS_BASE + fx].SetVolume(kAudioSampleOne);
    effectReturnRouting_[fx].returnGain =
        ToLogVolume(project->GetFXReturnVolume(fx), OutputEffectReturnMaximum);

    // Re-add every legal forward route even at zero gain so effect routing
    // always executes the maximum work supported by this slot's position.
    OutputEffectSlotAt(fx).ClearSendRouting();
    for (int destination = fx + 1; destination < OUTPUT_EFFECT_COUNT;
         ++destination) {
      const AudioSample sendGain = ToLogVolume(
          project->GetFXSend(fx, destination), OutputEffectSendMaximum);
      const bool routeAdded = OutputEffectSlotAt(fx).AddSendRouting(
          OutputEffectSlotAt(destination).GetAccumulationBuffer(), sendGain);
      NAssert(routeAdded);
    }
  }

  project->ClearMixRoutingDirty();
}

PICOTRACKER_FAST_CODE("mixer_service_prepare_render_pass")
void MixerService::PrepareRenderPass(Project *project) {
  NAssert(project);
  if (project->IsMixRoutingDirty()) {
    RefreshMixRouting(project);
  }

  for (int fx = 0; fx < OUTPUT_EFFECT_COUNT; ++fx) {
    OutputEffectSlotAt(fx).ClearAccumulationBuffer();
    OutputEffectSlotAt(fx).BeginRenderCycle(
        effectReturnRouting_[fx].returnGain);
  }
}

OutputEffectSlot &MixerService::OutputEffectSlotAt(int index) {
  NAssert(index >= 0 && index < OUTPUT_EFFECT_COUNT);
  NAssert(outputEffects_[index].has_value());
  return outputEffects_[index].value();
}

const OutputEffectSlot &MixerService::OutputEffectSlotAt(int index) const {
  NAssert(index >= 0 && index < OUTPUT_EFFECT_COUNT);
  NAssert(outputEffects_[index].has_value());
  return outputEffects_[index].value();
}

int MixerService::GetPlayedBufferPercentage() {
  return out_->GetPlayedBufferPercentage();
}

void MixerService::setRenderingMode(MixerServiceMode mode) {
  mode_ = mode;
  if (mode != MSM_AUDIO) {
    // in case proj name changed since last time paths were configured
    bool pathsResult = configureRenderPaths();
    if (!pathsResult) {
      Trace::Error(
          "[MixerService::setRenderingMode] Failed to set render paths");
    }
  }

  switch (mode) {
  case MSM_AUDIO:
    out_->SetRenderGain(kAudioSampleOne);
    out_->EnableRendering(false);
    for (int i = 0; i < MAX_BUS_COUNT; i++) {
      bus_[i].SetRenderGain(kAudioSampleOne);
      bus_[i].EnableRendering(false);
    };
    break;
  case MSM_FILE:
    out_->SetRenderGain(GetOutputRenderGain());
    out_->EnableRendering(true);
    for (int i = 0; i < MAX_BUS_COUNT; i++) {
      bus_[i].SetRenderGain(kAudioSampleOne);
      bus_[i].EnableRendering(false);
    };
    break;
  case MSM_FILESPLIT:
    out_->SetRenderGain(kAudioSampleOne);
    out_->EnableRendering(false);
    for (int i = 0; i < TRACK_MIX_BUS_COUNT; i++) {
      bus_[i].SetRenderGain(kAudioSampleOne);
      bus_[i].EnableRendering(true);
    };
    for (int i = 0; i < OUTPUT_EFFECT_COUNT; ++i) {
      if (i != kOutputEffectDelayIndex && i != kOutputEffectReverbIndex) {
        continue;
      }
      bus_[OUTPUT_EFFECT_BUS_BASE + i].SetRenderGain(kAudioSampleOne);
      bus_[OUTPUT_EFFECT_BUS_BASE + i].EnableRendering(true);
    }
    break;
  }
}

void MixerService::OnPlayerStart(MixerServiceMode mode) {
  setRenderingMode(mode);
};

void MixerService::OnPlayerStop() {
  // always reset back to audio mode when stopping
  setRenderingMode(MSM_AUDIO);
  ClearRenderTargetOverride();
};

bool MixerService::SetRenderTargetOverride(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }

  renderTargetOverridePath_ = path;
  return !renderTargetOverridePath_.is_truncated();
}

void MixerService::ClearRenderTargetOverride() {
  renderTargetOverridePath_.clear();
}

bool MixerService::configureRenderPaths() {
  if (!out_) {
    return false;
  }

  Player *player = Player::GetInstance();
  Project *project = player ? player->GetProject() : nullptr;
  if (!project) {
    Trace::Error("MIXERSERVICE", "Cannot configure render paths, project null");
    return false;
  }

  char projectname[MAX_PROJECT_NAME_LENGTH + 1];
  project->GetProjectName(projectname);

  FileSystemPath::PathString mixdownPath;
  char path[30 + MAX_PROJECT_NAME_LENGTH];
  if (!renderTargetOverridePath_.empty()) {
    out_->SetFileRenderer(renderTargetOverridePath_.c_str());
  } else {
    const auto mixdownPathStatus = BrowserPathUtils::BuildNextIndexedFilePath(
        RENDERS_DIR, "mixdown", ".wav", 99, mixdownPath);
    if (mixdownPathStatus ==
        BrowserPathUtils::NextIndexedFilePathStatus::Error) {
      Trace::Error("MIXERSERVICE", "Failed to build mixdown render path");
      return false;
    }
    if (mixdownPathStatus ==
        BrowserPathUtils::NextIndexedFilePathStatus::Found) {
      out_->SetFileRenderer(mixdownPath.c_str());
    }
  }

  for (int i = 0; i < TRACK_MIX_BUS_COUNT; i++) {
    npf_snprintf(path, sizeof(path), "/renders/%s-channel%d.wav", projectname,
                 i);
    bus_[i].SetFileRenderer(path);
  }
  for (int i = 0; i < OUTPUT_EFFECT_COUNT; ++i) {
    if (i != kOutputEffectDelayIndex && i != kOutputEffectReverbIndex) {
      continue;
    }
    npf_snprintf(path, sizeof(path), "/renders/%s-%s.wav", projectname,
                 GetOutputEffectFileStemForSlot(i));
    bus_[OUTPUT_EFFECT_BUS_BASE + i].SetFileRenderer(path);
  }
  return true;
}

void MixerService::Execute(FourCC id, float value) {
  if (value > 0.5) {
    Audio *audio = Audio::GetInstance();
    int volume = audio->GetMixerVolume();
    switch (id) {
    case FourCC::TrigVolumeIncrease:
      if (volume < 100)
        volume += 1;
      break;
    case FourCC::TrigVolumeDecrease:
      if (volume > 0)
        volume -= 1;
      break;
    };
    audio->SetMixerVolume(volume);
  };
}

AudioOut *MixerService::GetAudioOut() { return out_; };

PICOTRACKER_FAST_CODE("mixer_service_lock")
void MixerService::Lock() { sync_->Lock(); }

PICOTRACKER_FAST_CODE("mixer_service_unlock")
void MixerService::Unlock() { sync_->Unlock(); }
