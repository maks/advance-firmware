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

#include "Project.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Model/FirmwareVersion.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/Player.h"
#include "Application/Player/SyncMaster.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Groove.h"
#include "Scale.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/io/Status.h"
#include "Table.h"
#include "config/MemorySections.h"

#include <math.h>

#define DEFAULT_CHANNEL_FX_SEND 0
#define DEFAULT_FX_RETURN_VOLUME OutputEffectReturnMaximum
#define DEFAULT_FX_ROUTING_SEND 0
#define DEFAULT_PREVIEW_VOLUME 60

#define DATA_UNUSED_VALUE 0xFF

namespace {
constexpr uint8_t DefaultMixerVolume = Project::MixerVolumeMaximum;
constexpr int32_t LegacyChannelVolumeMaximum = 99;
constexpr int32_t LegacyMasterVolumeMaximum = 100;
constexpr const char *LegacyMasterVolumeName = "master";
constexpr const char *LegacyChannelVolumeNames[SONG_CHANNEL_COUNT] = {
    "channel1vol", "channel2vol", "channel3vol", "channel4vol",
    "channel5vol", "channel6vol", "channel7vol", "channel8vol"};

int32_t GetChannelVolumeIndex(FourCC id) {
  if (id < FourCC::VarChannel1Volume || id > FourCC::VarChannel8Volume) {
    return -1;
  }
  return static_cast<int32_t>(id.get_value() - FourCC::VarChannel1Volume);
}

int32_t GetLegacyChannelVolumeIndex(const char *name) {
  for (int32_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    if (!strcmp(name, LegacyChannelVolumeNames[channel])) {
      return channel;
    }
  }
  return -1;
}

int32_t ScaleLegacyMixerVolume(int32_t value, int32_t legacyMaximum) {
  const int32_t clamped = std::clamp(value, int32_t(0), legacyMaximum);
  return (clamped * Project::MixerVolumeMaximum + legacyMaximum / 2) /
         legacyMaximum;
}

bool IsMixRoutingVariable(FourCC id) {
  if (id == FourCC::VarMasterVolume) {
    return true;
  }
  if (id == FourCC::VarLineInMixLevel) {
    return true;
  }
  if (id >= FourCC::VarChannel1Volume && id <= FourCC::VarChannel8Volume) {
    return true;
  }
  if (id >= FourCC::VarChannel1FX1Send && id <= FourCC::VarChannel8FX4Send) {
    return true;
  }
  if (id >= FourCC::VarFX1Volume && id <= FourCC::VarFX3ToFX4Send) {
    return true;
  }
  return false;
}

} // namespace

Project::Project(const char *name)
    : Persistent("PROJECT"), VariableContainer(&variables_), song_(),
      tempoNudge_(0), tempo_(FourCC::VarTempo, DEFAULT_TEMPO),
      masterVolume_(FourCC::VarMasterVolume, DefaultMixerVolume),
      lineInMixLevel_(FourCC::VarLineInMixLevel, 0),
      channelVolume1_(FourCC::VarChannel1Volume, DefaultMixerVolume),
      channelVolume2_(FourCC::VarChannel2Volume, DefaultMixerVolume),
      channelVolume3_(FourCC::VarChannel3Volume, DefaultMixerVolume),
      channelVolume4_(FourCC::VarChannel4Volume, DefaultMixerVolume),
      channelVolume5_(FourCC::VarChannel5Volume, DefaultMixerVolume),
      channelVolume6_(FourCC::VarChannel6Volume, DefaultMixerVolume),
      channelVolume7_(FourCC::VarChannel7Volume, DefaultMixerVolume),
      channelVolume8_(FourCC::VarChannel8Volume, DefaultMixerVolume),
      channel1FX1Send_(FourCC::VarChannel1FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel2FX1Send_(FourCC::VarChannel2FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel3FX1Send_(FourCC::VarChannel3FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel4FX1Send_(FourCC::VarChannel4FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel5FX1Send_(FourCC::VarChannel5FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel6FX1Send_(FourCC::VarChannel6FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel7FX1Send_(FourCC::VarChannel7FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel8FX1Send_(FourCC::VarChannel8FX1Send, DEFAULT_CHANNEL_FX_SEND),
      channel1FX2Send_(FourCC::VarChannel1FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel2FX2Send_(FourCC::VarChannel2FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel3FX2Send_(FourCC::VarChannel3FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel4FX2Send_(FourCC::VarChannel4FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel5FX2Send_(FourCC::VarChannel5FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel6FX2Send_(FourCC::VarChannel6FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel7FX2Send_(FourCC::VarChannel7FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel8FX2Send_(FourCC::VarChannel8FX2Send, DEFAULT_CHANNEL_FX_SEND),
      channel1FX3Send_(FourCC::VarChannel1FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel2FX3Send_(FourCC::VarChannel2FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel3FX3Send_(FourCC::VarChannel3FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel4FX3Send_(FourCC::VarChannel4FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel5FX3Send_(FourCC::VarChannel5FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel6FX3Send_(FourCC::VarChannel6FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel7FX3Send_(FourCC::VarChannel7FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel8FX3Send_(FourCC::VarChannel8FX3Send, DEFAULT_CHANNEL_FX_SEND),
      channel1FX4Send_(FourCC::VarChannel1FX4Send, DEFAULT_CHANNEL_FX_SEND),
      channel2FX4Send_(FourCC::VarChannel2FX4Send, DEFAULT_CHANNEL_FX_SEND),
      channel3FX4Send_(FourCC::VarChannel3FX4Send, DEFAULT_CHANNEL_FX_SEND),
      channel4FX4Send_(FourCC::VarChannel4FX4Send, DEFAULT_CHANNEL_FX_SEND),
      channel5FX4Send_(FourCC::VarChannel5FX4Send, DEFAULT_CHANNEL_FX_SEND),
      channel6FX4Send_(FourCC::VarChannel6FX4Send, DEFAULT_CHANNEL_FX_SEND),
      channel7FX4Send_(FourCC::VarChannel7FX4Send, DEFAULT_CHANNEL_FX_SEND),
      channel8FX4Send_(FourCC::VarChannel8FX4Send, DEFAULT_CHANNEL_FX_SEND),
      fx1Volume_(FourCC::VarFX1Volume, DEFAULT_FX_RETURN_VOLUME),
      fx2Volume_(FourCC::VarFX2Volume, DEFAULT_FX_RETURN_VOLUME),
      fx3Volume_(FourCC::VarFX3Volume, DEFAULT_FX_RETURN_VOLUME),
      fx4Volume_(FourCC::VarFX4Volume, DEFAULT_FX_RETURN_VOLUME),
      fx1ToFX2Send_(FourCC::VarFX1ToFX2Send, DEFAULT_FX_ROUTING_SEND),
      fx1ToFX3Send_(FourCC::VarFX1ToFX3Send, DEFAULT_FX_ROUTING_SEND),
      fx1ToFX4Send_(FourCC::VarFX1ToFX4Send, DEFAULT_FX_ROUTING_SEND),
      fx2ToFX3Send_(FourCC::VarFX2ToFX3Send, DEFAULT_FX_ROUTING_SEND),
      fx2ToFX4Send_(FourCC::VarFX2ToFX4Send, DEFAULT_FX_ROUTING_SEND),
      fx3ToFX4Send_(FourCC::VarFX3ToFX4Send, DEFAULT_FX_ROUTING_SEND),
      wrap_(FourCC::VarWrap, false), transpose_(FourCC::VarTranspose, 0),
      scale_(FourCC::VarScale, scaleNames, numScales, 0),
      scaleRoot_(FourCC::VarScaleRoot, noteNames, 12, 0),
      projectName_(FourCC::VarProjectName, name),
      previewVolume_(FourCC::VarPreviewVolume, DEFAULT_PREVIEW_VOLUME),
      mixRoutingDirty_(true) {

  outputEffects_[kOutputEffectSlot1Index] = &fx1Effect_;
  outputEffects_[kOutputEffectSlot2Index] = &fx2Effect_;
  outputEffects_[kOutputEffectDelayIndex] = &delayEffect_;
  outputEffects_[kOutputEffectReverbIndex] = &reverbEffect_;

  this->variables_.insert(variables_.end(), &tempo_);
  this->variables_.insert(variables_.end(), &masterVolume_);
  this->variables_.insert(variables_.end(), &lineInMixLevel_);

  // Add individual channel volume variables to the container
  this->variables_.insert(variables_.end(), &channelVolume1_);
  this->variables_.insert(variables_.end(), &channelVolume2_);
  this->variables_.insert(variables_.end(), &channelVolume3_);
  this->variables_.insert(variables_.end(), &channelVolume4_);
  this->variables_.insert(variables_.end(), &channelVolume5_);
  this->variables_.insert(variables_.end(), &channelVolume6_);
  this->variables_.insert(variables_.end(), &channelVolume7_);
  this->variables_.insert(variables_.end(), &channelVolume8_);
  this->variables_.insert(variables_.end(), &channel1FX1Send_);
  this->variables_.insert(variables_.end(), &channel2FX1Send_);
  this->variables_.insert(variables_.end(), &channel3FX1Send_);
  this->variables_.insert(variables_.end(), &channel4FX1Send_);
  this->variables_.insert(variables_.end(), &channel5FX1Send_);
  this->variables_.insert(variables_.end(), &channel6FX1Send_);
  this->variables_.insert(variables_.end(), &channel7FX1Send_);
  this->variables_.insert(variables_.end(), &channel8FX1Send_);
  this->variables_.insert(variables_.end(), &channel1FX2Send_);
  this->variables_.insert(variables_.end(), &channel2FX2Send_);
  this->variables_.insert(variables_.end(), &channel3FX2Send_);
  this->variables_.insert(variables_.end(), &channel4FX2Send_);
  this->variables_.insert(variables_.end(), &channel5FX2Send_);
  this->variables_.insert(variables_.end(), &channel6FX2Send_);
  this->variables_.insert(variables_.end(), &channel7FX2Send_);
  this->variables_.insert(variables_.end(), &channel8FX2Send_);
  this->variables_.insert(variables_.end(), &channel1FX3Send_);
  this->variables_.insert(variables_.end(), &channel2FX3Send_);
  this->variables_.insert(variables_.end(), &channel3FX3Send_);
  this->variables_.insert(variables_.end(), &channel4FX3Send_);
  this->variables_.insert(variables_.end(), &channel5FX3Send_);
  this->variables_.insert(variables_.end(), &channel6FX3Send_);
  this->variables_.insert(variables_.end(), &channel7FX3Send_);
  this->variables_.insert(variables_.end(), &channel8FX3Send_);
  this->variables_.insert(variables_.end(), &channel1FX4Send_);
  this->variables_.insert(variables_.end(), &channel2FX4Send_);
  this->variables_.insert(variables_.end(), &channel3FX4Send_);
  this->variables_.insert(variables_.end(), &channel4FX4Send_);
  this->variables_.insert(variables_.end(), &channel5FX4Send_);
  this->variables_.insert(variables_.end(), &channel6FX4Send_);
  this->variables_.insert(variables_.end(), &channel7FX4Send_);
  this->variables_.insert(variables_.end(), &channel8FX4Send_);
  this->variables_.insert(variables_.end(), &fx1Volume_);
  this->variables_.insert(variables_.end(), &fx2Volume_);
  this->variables_.insert(variables_.end(), &fx3Volume_);
  this->variables_.insert(variables_.end(), &fx4Volume_);
  this->variables_.insert(variables_.end(), &fx1ToFX2Send_);
  this->variables_.insert(variables_.end(), &fx1ToFX3Send_);
  this->variables_.insert(variables_.end(), &fx1ToFX4Send_);
  this->variables_.insert(variables_.end(), &fx2ToFX3Send_);
  this->variables_.insert(variables_.end(), &fx2ToFX4Send_);
  this->variables_.insert(variables_.end(), &fx3ToFX4Send_);

  this->variables_.insert(variables_.end(), &wrap_);
  this->variables_.insert(variables_.end(), &transpose_);
  this->variables_.insert(variables_.end(), &scale_);
  scale_.SetInt(0);
  this->variables_.insert(variables_.end(), &scaleRoot_);
  scaleRoot_.SetInt(0); // Default to C (0)
  this->variables_.insert(variables_.end(), &projectName_);
  this->variables_.insert(variables_.end(), &previewVolume_);
  for (Variable *parameter : delayEffect_.GetVariables()) {
    this->variables_.insert(variables_.end(), parameter);
  }
  for (Variable *parameter : reverbEffect_.GetVariables()) {
    this->variables_.insert(variables_.end(), parameter);
  }

  WatchedVariable *mixRoutingVars[] = {
      &masterVolume_,
      &channelVolume1_,
      &channelVolume2_,
      &channelVolume3_,
      &channelVolume4_,
      &channelVolume5_,
      &channelVolume6_,
      &channelVolume7_,
      &channelVolume8_,
      &channel1FX1Send_,
      &channel2FX1Send_,
      &channel3FX1Send_,
      &channel4FX1Send_,
      &channel5FX1Send_,
      &channel6FX1Send_,
      &channel7FX1Send_,
      &channel8FX1Send_,
      &channel1FX2Send_,
      &channel2FX2Send_,
      &channel3FX2Send_,
      &channel4FX2Send_,
      &channel5FX2Send_,
      &channel6FX2Send_,
      &channel7FX2Send_,
      &channel8FX2Send_,
      &channel1FX3Send_,
      &channel2FX3Send_,
      &channel3FX3Send_,
      &channel4FX3Send_,
      &channel5FX3Send_,
      &channel6FX3Send_,
      &channel7FX3Send_,
      &channel8FX3Send_,
      &channel1FX4Send_,
      &channel2FX4Send_,
      &channel3FX4Send_,
      &channel4FX4Send_,
      &channel5FX4Send_,
      &channel6FX4Send_,
      &channel7FX4Send_,
      &channel8FX4Send_,
      &fx1Volume_,
      &fx2Volume_,
      &fx3Volume_,
      &fx4Volume_,
      &fx1ToFX2Send_,
      &fx1ToFX3Send_,
      &fx1ToFX4Send_,
      &fx2ToFX3Send_,
      &fx2ToFX4Send_,
      &fx3ToFX4Send_,
  };
  for (WatchedVariable *var : mixRoutingVars) {
    var->AddObserver(*this);
  }

  // The line input joins the mix at the master output, so its level is mix
  // routing as well.
  lineInMixLevel_.AddObserver(*this);

  // Project name is now managed through the WatchedVariable

  // look if we can find a sav file

  // Makes sure the tables exists for restoring

  TableHolder::GetInstance();

  Groove::GetInstance()->Clear();

  tempoTapCount_ = 0;

  Status::Set("About to load project");
};

Project::~Project() {}

void Project::Load(const char *name) {
  tempoNudge_ = 0;
  tempoTapCount_ = 0;
  for (int i = 0; i < MAX_TAP; i++) {
    lastTap_[i] = 0;
  }

  song_.Reset();
  instrumentBank_.Reset();
  delayEffect_.ResetParameters();
  delayEffect_.Reset();
  reverbEffect_.ResetParameters();
  reverbEffect_.Reset();
  Groove::GetInstance()->Clear();

  tempo_.Reset();
  masterVolume_.Reset();
  lineInMixLevel_.Reset();
  channelVolume1_.Reset();
  channelVolume2_.Reset();
  channelVolume3_.Reset();
  channelVolume4_.Reset();
  channelVolume5_.Reset();
  channelVolume6_.Reset();
  channelVolume7_.Reset();
  channelVolume8_.Reset();
  channel1FX1Send_.Reset();
  channel2FX1Send_.Reset();
  channel3FX1Send_.Reset();
  channel4FX1Send_.Reset();
  channel5FX1Send_.Reset();
  channel6FX1Send_.Reset();
  channel7FX1Send_.Reset();
  channel8FX1Send_.Reset();
  channel1FX2Send_.Reset();
  channel2FX2Send_.Reset();
  channel3FX2Send_.Reset();
  channel4FX2Send_.Reset();
  channel5FX2Send_.Reset();
  channel6FX2Send_.Reset();
  channel7FX2Send_.Reset();
  channel8FX2Send_.Reset();
  channel1FX3Send_.Reset();
  channel2FX3Send_.Reset();
  channel3FX3Send_.Reset();
  channel4FX3Send_.Reset();
  channel5FX3Send_.Reset();
  channel6FX3Send_.Reset();
  channel7FX3Send_.Reset();
  channel8FX3Send_.Reset();
  channel1FX4Send_.Reset();
  channel2FX4Send_.Reset();
  channel3FX4Send_.Reset();
  channel4FX4Send_.Reset();
  channel5FX4Send_.Reset();
  channel6FX4Send_.Reset();
  channel7FX4Send_.Reset();
  channel8FX4Send_.Reset();
  fx1Volume_.Reset();
  fx2Volume_.Reset();
  fx3Volume_.Reset();
  fx4Volume_.Reset();
  fx1ToFX2Send_.Reset();
  fx1ToFX3Send_.Reset();
  fx1ToFX4Send_.Reset();
  fx2ToFX3Send_.Reset();
  fx2ToFX4Send_.Reset();
  fx3ToFX4Send_.Reset();
  wrap_.Reset();
  transpose_.Reset();
  scale_.Reset();
  scaleRoot_.Reset();
  previewVolume_.Reset();

  if (name) {
    projectName_.SetString(name, true);
  } else {
    projectName_.SetString("", true);
  }

  mixRoutingDirty_ = true;

  Status::Set("About to load project");
}

int Project::GetScale() {
  Variable *v = FindVariable(FourCC::VarScale);
  NAssert(v);
  return v->GetInt();
}

uint8_t Project::GetScaleRoot() {
  Variable *v = FindVariable(FourCC::VarScaleRoot);
  NAssert(v);
  return v->GetInt();
}

PICOTRACKER_FAST_CODE("project_get_tempo")
int Project::GetTempo() {
  Variable *v = FindVariable(FourCC::VarTempo);
  NAssert(v);
  int tempo = v->GetInt() + tempoNudge_;
  return tempo;
};

int Project::GetMasterVolume() {
  Variable *v = FindVariable(FourCC::VarMasterVolume);
  NAssert(v);
  return v->GetInt();
}

int Project::GetLineInMixLevel() {
  return std::clamp(lineInMixLevel_.GetInt(), 0,
                    static_cast<int>(MixerVolumeMaximum));
}

int Project::GetChannelVolume(int channel) {
  switch (channel) {
  case 0:
    return channelVolume1_.GetInt();
  case 1:
    return channelVolume2_.GetInt();
  case 2:
    return channelVolume3_.GetInt();
  case 3:
    return channelVolume4_.GetInt();
  case 4:
    return channelVolume5_.GetInt();
  case 5:
    return channelVolume6_.GetInt();
  case 6:
    return channelVolume7_.GetInt();
  case 7:
    return channelVolume8_.GetInt();
  default:
    NAssert(false);
    return 0;
  }
};

int Project::GetChannelFXSend(int channel, int fx) {
  switch (fx) {
  case 0:
    switch (channel) {
    case 0:
      return channel1FX1Send_.GetInt();
    case 1:
      return channel2FX1Send_.GetInt();
    case 2:
      return channel3FX1Send_.GetInt();
    case 3:
      return channel4FX1Send_.GetInt();
    case 4:
      return channel5FX1Send_.GetInt();
    case 5:
      return channel6FX1Send_.GetInt();
    case 6:
      return channel7FX1Send_.GetInt();
    case 7:
      return channel8FX1Send_.GetInt();
    default:
      NAssert(false);
      return 0;
    }
  case 1:
    switch (channel) {
    case 0:
      return channel1FX2Send_.GetInt();
    case 1:
      return channel2FX2Send_.GetInt();
    case 2:
      return channel3FX2Send_.GetInt();
    case 3:
      return channel4FX2Send_.GetInt();
    case 4:
      return channel5FX2Send_.GetInt();
    case 5:
      return channel6FX2Send_.GetInt();
    case 6:
      return channel7FX2Send_.GetInt();
    case 7:
      return channel8FX2Send_.GetInt();
    default:
      NAssert(false);
      return 0;
    }
  case 2:
    switch (channel) {
    case 0:
      return channel1FX3Send_.GetInt();
    case 1:
      return channel2FX3Send_.GetInt();
    case 2:
      return channel3FX3Send_.GetInt();
    case 3:
      return channel4FX3Send_.GetInt();
    case 4:
      return channel5FX3Send_.GetInt();
    case 5:
      return channel6FX3Send_.GetInt();
    case 6:
      return channel7FX3Send_.GetInt();
    case 7:
      return channel8FX3Send_.GetInt();
    default:
      NAssert(false);
      return 0;
    }
  case 3:
    switch (channel) {
    case 0:
      return channel1FX4Send_.GetInt();
    case 1:
      return channel2FX4Send_.GetInt();
    case 2:
      return channel3FX4Send_.GetInt();
    case 3:
      return channel4FX4Send_.GetInt();
    case 4:
      return channel5FX4Send_.GetInt();
    case 5:
      return channel6FX4Send_.GetInt();
    case 6:
      return channel7FX4Send_.GetInt();
    case 7:
      return channel8FX4Send_.GetInt();
    default:
      NAssert(false);
      return 0;
    }
  default:
    NAssert(false);
    return 0;
  }
}

int Project::GetFXReturnVolume(int fx) {
  switch (fx) {
  case 0:
    return fx1Volume_.GetInt();
  case 1:
    return fx2Volume_.GetInt();
  case 2:
    return fx3Volume_.GetInt();
  case 3:
    return fx4Volume_.GetInt();
  default:
    NAssert(false);
    return 0;
  }
}

int Project::GetFXSend(int sourceFx, int destinationFx) {
  if (sourceFx == 0 && destinationFx == 1) {
    return fx1ToFX2Send_.GetInt();
  }
  if (sourceFx == 0 && destinationFx == 2) {
    return fx1ToFX3Send_.GetInt();
  }
  if (sourceFx == 0 && destinationFx == 3) {
    return fx1ToFX4Send_.GetInt();
  }
  if (sourceFx == 1 && destinationFx == 2) {
    return fx2ToFX3Send_.GetInt();
  }
  if (sourceFx == 1 && destinationFx == 3) {
    return fx2ToFX4Send_.GetInt();
  }
  if (sourceFx == 2 && destinationFx == 3) {
    return fx3ToFX4Send_.GetInt();
  }

  NAssert(false);
  return 0;
}

OutputEffect *Project::GetOutputEffect(int fx) {
  if (fx < 0 || fx >= OUTPUT_EFFECT_COUNT) {
    return nullptr;
  }
  return outputEffects_[fx];
}

const OutputEffect *Project::GetOutputEffect(int fx) const {
  if (fx < 0 || fx >= OUTPUT_EFFECT_COUNT) {
    return nullptr;
  }
  return outputEffects_[fx];
}

bool Project::IsMixRoutingDirty() const { return mixRoutingDirty_; }

void Project::ClearMixRoutingDirty() { mixRoutingDirty_ = false; }

void Project::GetProjectName(char *name) {
  Variable *v = FindVariable(FourCC::VarProjectName);
  strncpy(name, v->GetString().c_str(), MAX_PROJECT_NAME_LENGTH);
  name[MAX_PROJECT_NAME_LENGTH] = '\0';
}

void Project::SetProjectName(char *name) {
  Variable *v = FindVariable(FourCC::VarProjectName);
  char buffer[MAX_PROJECT_NAME_LENGTH + 1];
  strncpy(buffer, name, MAX_PROJECT_NAME_LENGTH);
  buffer[MAX_PROJECT_NAME_LENGTH] = '\0';
  v->SetString(buffer, true);
}

void Project::NudgeTempo(int value) {
  if ((GetTempo() + tempoNudge_) > 0) {
    tempoNudge_ += value;
  }
};

PICOTRACKER_FAST_CODE("project_trigger")
void Project::Trigger() {
  if (tempoNudge_ != 0) {
    if (tempoNudge_ > 0) {
      tempoNudge_--;
    } else {
      tempoNudge_++;
    };
  }
};

int Project::GetTranspose() {
  Variable *v = FindVariable(FourCC::VarTranspose);
  NAssert(v);
  int result = v->GetInt();
  if (result > 0x80) {
    result -= 128;
  }
  return result;
};

bool Project::Wrap() {
  Variable *v = FindVariable(FourCC::VarWrap);
  NAssert(v);
  return v->GetBool();
};

InstrumentBank *Project::GetInstrumentBank() { return &instrumentBank_; };

// Changing a NONE slot is always safe. Other instrument types can only be
// replaced during playback when no project data, live input, or mixer voice
// refers to them.
bool Project::CanChangeInstrumentTypeWhilePlaying(
    uint8_t instrumentId) const {
  if (instrumentId >= MAX_INSTRUMENT_COUNT) {
    return false;
  }

  I_Instrument *instrument =
      instrumentBank_.InstrumentsList()[instrumentId];
  if (instrument == nullptr) {
    return false;
  }
  if (instrument->GetType() == IT_NONE) {
    return true;
  }

  const Song::InstrumentUsageMap used = song_.GetInstrumentUsage();
  if (used[instrumentId]) {
    return false;
  }

  MidiService *midiService = MidiService::GetInstance();
  if (midiService != nullptr) {
    for (uint8_t songChannel = 0; songChannel < SONG_CHANNEL_COUNT;
         songChannel++) {
      const uint8_t midiInputChannel =
          song_.GetMidiInputChannel(songChannel);
      if (midiInputChannel != MIDI_INPUT_CHANNEL_NONE &&
          midiService->IsInputProgramSelected(midiInputChannel - 1U,
                                              instrumentId)) {
        return false;
      }
    }
  }

  Player *player = Player::GetExistingInstance();
  return player == nullptr || !player->IsInstrumentInUse(instrument);
}

void Project::Update(Observable &o, I_ObservableData *d) {
  if (!d) {
    return;
  }

  FourCC id =
      FourCC(static_cast<FourCC::value_type>(reinterpret_cast<uintptr_t>(d)));
  if (IsMixRoutingVariable(id)) {
    mixRoutingDirty_ = true;
  }
}

void Project::Purge() {

  song_.chain_.ClearAllocation();
  song_.phrase_.ClearAllocation();

  unsigned char *data = song_.data_;
  for (int i = 0; i < 256 * SONG_CHANNEL_COUNT; i++) {
    if (*data != DATA_UNUSED_VALUE) {
      song_.chain_.SetUsed(*data);
    }
    data++;
  }

  data = song_.chain_.data_;
  unsigned char *data2 = song_.chain_.transpose_;

  for (int i = 0; i < CHAIN_COUNT; i++) {

    if (song_.chain_.IsUsed(i)) {
      for (int j = 0; j < 16; j++) {
        if (*data != DATA_UNUSED_VALUE) {
          song_.phrase_.SetUsed(*data);
        }
        data++;
        data2++;
      }
    } else {

      for (int j = 0; j < 16; j++) {
        *data++ = DATA_UNUSED_VALUE;
        *data2++ = 0x00;
      }
    }
  }

  data = song_.phrase_.note_;
  data2 = song_.phrase_.instr_;

  TrackerCommand *cmd1 = song_.phrase_.cmd1_;
  ushort *param1 = song_.phrase_.param1_;
  TrackerCommand *cmd2 = song_.phrase_.cmd2_;
  ushort *param2 = song_.phrase_.param2_;

  for (int i = 0; i < PHRASE_COUNT; i++) {
    for (int j = 0; j < 16; j++) {
      if (!song_.phrase_.IsUsed(i)) {
        *data = DATA_UNUSED_VALUE;
        *data2 = DATA_UNUSED_VALUE;
        *cmd1 = TrackerCommand::InstrumentCommandNone;
        *param1 = 0;
        *cmd2 = TrackerCommand::InstrumentCommandNone;
        *param2 = 0;
      }
      data++;
      data2++;
      cmd1++;
      param1++;
      cmd2++;
      param2++;
    };
  }
};

// Returns the first instrument index using the sample, or -1 if unused.
int32_t Project::SampleInUse(
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> filename) {
  InstrumentBank *bank = GetInstrumentBank();

  for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
    I_Instrument *instrument = bank->GetInstrument(i);
    if (instrument->GetType() == IT_SAMPLE) {
      SampleInstrument *si = static_cast<SampleInstrument *>(instrument);
      if (si->GetSampleFileName() == filename) {
        return i;
      }
    } else if (instrument->GetType() == IT_SAMPLE_SOURCE) {
      SampleSourceInstrument *ssi =
          static_cast<SampleSourceInstrument *>(instrument);
      if (ssi->GetSampleFileName() == filename) {
        return i;
      }
    }
  }
  return -1;
}

// remove all samples that are not in use by an instrument
void Project::PurgeSamples() {
  // clear used flag
  bool isUsed[MAX_SAMPLES] = {false};

  // flag all samples actually used
  InstrumentBank *bank = GetInstrumentBank();
  for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
    I_Instrument *instrument = bank->GetInstrument(i);
    if (instrument->GetType() == IT_SAMPLE) {
      SampleInstrument *si = static_cast<SampleInstrument *>(instrument);
      int index = si->GetSampleIndex();
      if (index >= 0) {
        isUsed[index] = true;
      }
    } else if (instrument->GetType() == IT_SAMPLE_SOURCE) {
      SampleSourceInstrument *ssi =
          static_cast<SampleSourceInstrument *>(instrument);
      int index = ssi->GetSampleIndex();
      if (index >= 0) {
        isUsed[index] = true;
      }
    }
  }

  // Now remove all unused samples from disk
  int purged = 0;
  SamplePool *sp = SamplePool::GetInstance();
  for (int i = 0; i < MAX_SAMPLES; i++) {
    if ((!isUsed[i]) && (sp->GetSource(i - purged))) {
      sp->PurgeSample(i - purged, projectName_.GetString().c_str());
      Trace::Debug("Purged sample [%d]", i - purged);
      purged++;
    } else {
      Trace::Debug("Sample [%d] not purged", i);
    }
  };
  Trace::Debug("Purged %d samples", purged);
}

void Project::PurgeInstruments() {
  const Song::InstrumentUsageMap used = song_.GetInstrumentUsage();

  InstrumentBank *bank = GetInstrumentBank();
  for (uint8_t instrumentId = 0; instrumentId < MAX_INSTRUMENT_COUNT;
       instrumentId++) {
    if (!used[instrumentId]) {
      I_Instrument *instrument = bank->GetInstrument(instrumentId);
      if (instrument->GetType() == IT_SAMPLE) {
        SampleInstrument *sampleInstrument =
            static_cast<SampleInstrument *>(instrument);
        sampleInstrument->AssignSample(NO_SAMPLE);
      } else if (instrument->GetType() == IT_SAMPLE_SOURCE) {
        SampleSourceInstrument *sampleSourceInstrument =
            static_cast<SampleSourceInstrument *>(instrument);
        sampleSourceInstrument->AssignSample(-1);
      }
      // we dont reorder indexes on release so safe to call inside this loop
      bank->releaseInstrument(instrumentId);
      Trace::Debug("Set unused instrument slot [%d] to NONE", instrumentId);
    }
  }
};

void Project::RestoreContent(PersistencyDocument *doc) {
  bool newMasterVolumeFound = false;
  bool legacyMasterVolumeFound = false;
  int32_t legacyMasterVolume = 0;
  bool newChannelVolumesFound[SONG_CHANNEL_COUNT] = {};
  bool legacyChannelVolumesFound[SONG_CHANNEL_COUNT] = {};
  int32_t legacyChannelVolumes[SONG_CHANNEL_COUNT] = {};

  bool attr = doc->NextAttribute();
  while (attr) {
    attr = doc->NextAttribute();
  }

  // Now loop on all variables
  bool elem = doc->FirstChild();
  while (elem) {
    bool attr = doc->NextAttribute();
    char name[MAX_VARIABLE_STRING_LENGTH + 1] = {};
    char value[MAX_VARIABLE_STRING_LENGTH + 1] = {};
    while (attr) {
      if (!strcmp(doc->attrname_, "NAME")) {
        strcpy(name, doc->attrval_);
      }
      if (!strcmp(doc->attrname_, "VALUE")) {
        strcpy(value, doc->attrval_);
      }
      attr = doc->NextAttribute();
    }
    Variable *v = FindVariable(name);
    // Project name now comes from the directory, so ignore any persisted value.
    if (v && v->GetID() != FourCC::VarProjectName) {
      if (v->GetID() == FourCC::VarTempo) {
        v->SetInt(std::clamp(atoi(value), int(MIN_TEMPO), int(MAX_TEMPO)));
      } else if (v->GetID() == FourCC::VarMasterVolume) {
        newMasterVolumeFound = true;
        v->SetInt(std::clamp(atoi(value), 0,
                             static_cast<int>(MixerVolumeMaximum)));
      } else if (const int32_t channel = GetChannelVolumeIndex(v->GetID());
                 channel >= 0) {
        newChannelVolumesFound[channel] = true;
        v->SetInt(std::clamp(atoi(value), 0,
                             static_cast<int>(MixerVolumeMaximum)));
      } else {
        v->SetString(value);
      }
    } else if (!strcmp(name, LegacyMasterVolumeName)) {
      legacyMasterVolumeFound = true;
      legacyMasterVolume = atoi(value);
    } else if (const int32_t channel = GetLegacyChannelVolumeIndex(name);
               channel >= 0) {
      legacyChannelVolumesFound[channel] = true;
      legacyChannelVolumes[channel] = atoi(value);
    }
    elem = doc->NextSibling();
  }

  if (!newMasterVolumeFound && legacyMasterVolumeFound) {
    masterVolume_.SetInt(ScaleLegacyMixerVolume(
        legacyMasterVolume, LegacyMasterVolumeMaximum));
  }

  WatchedVariable *channelVolumes[SONG_CHANNEL_COUNT] = {
      &channelVolume1_, &channelVolume2_, &channelVolume3_, &channelVolume4_,
      &channelVolume5_, &channelVolume6_, &channelVolume7_, &channelVolume8_};
  for (int32_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    if (!newChannelVolumesFound[channel] &&
        legacyChannelVolumesFound[channel]) {
      channelVolumes[channel]->SetInt(ScaleLegacyMixerVolume(
          legacyChannelVolumes[channel], LegacyChannelVolumeMaximum));
    }
  }
  mixRoutingDirty_ = true;
}

void Project::SaveContent(tinyxml2::XMLPrinter *printer) {

  // store project version
  const FirmwareNumberString firmwareVersion = FormatFirmwareVersionNumber();
  printer->PushAttribute("VERSION", firmwareVersion.c_str());

  // save all of the project's parameters
  auto it = variables_.begin();
  for (size_t i = 0; i < variables_.size(); i++) {
    Variable *currentVar = *it;
    // The project name comes from its directory.
    if (currentVar->GetID() == FourCC::VarProjectName) {
      it++;
      continue;
    }

    printer->OpenElement("PARAMETER");
    printer->PushAttribute("NAME", currentVar->GetName());
    printer->PushAttribute("VALUE", currentVar->GetString().c_str());
    printer->CloseElement();
    it++;
  }
};

void Project::OnTempoTap() {

  unsigned long now = System::GetInstance()->GetClock();

  if (tempoTapCount_ != 0) {
    // count last tick tempo and see if in range
    unsigned millisec = now - lastTap_[tempoTapCount_ - 1];
    int t = int(60000 / (float)millisec);
    if (t > 30) {
      if (tempoTapCount_ == MAX_TAP) {
        for (int i = 0; i < int(tempoTapCount_) - 1; i++) {
          lastTap_[i] = lastTap_[i + 1];
        }
      } else {
        tempoTapCount_++;
      }
      int tempo =
          int(60000 * (tempoTapCount_ - 1) / (float)(now - lastTap_[0]));
      Variable *v = FindVariable(FourCC::VarTempo);
      // ensure tempo is within range
      tempo = std::clamp((unsigned short)tempo, MIN_TEMPO, MAX_TEMPO);
      v->SetInt(tempo);
    } else {
      tempoTapCount_ = 1;
    }
  } else {
    tempoTapCount_ = 1;
  }
  lastTap_[tempoTapCount_ - 1] = now;
}
