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

#include "StarLoomInstrument.h"
#include "Application/Instruments/Filters.h"
#include "Application/Model/Table.h"
#include "Application/Player/PlayerMixer.h" // For MIX_BUFFER_SIZE.. kick out pls
#include "Application/Player/SyncMaster.h"
#include "CommandList.h"
#include "Externals/braids/macro_oscillator.h"
#include "Externals/braids/settings.h"
#include "Externals/braids/signature_waveshaper.h"
#include "I_Instrument.h"
#include "Services/Audio/Audio.h"
#include "StarLoomInstrumentDSP.h"
#include "StarLoomInstrumentShapeOptions.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "System/io/Status.h"
#include <assert.h>
#include <cstring>

namespace {

constexpr size_t kOscillatorCount = 2;
constexpr size_t kBraidsBlockSize = 24;
constexpr size_t kCommandRampCount = 7;
constexpr uint32_t kFallbackDeviceIdentitySeed = 0x5049434FU;
const char *const kInteractionModeNames[] = {"MIX", "RING", "DIFF"};
const char *const kSignatureNames[] = {"OFF", "1", "2", "3", "FULL"};
static_assert(
    sizeof(kInteractionModeNames) / sizeof(kInteractionModeNames[0]) ==
    static_cast<size_t>(StarLoomInstrumentDSP::kInteractionModeCount));

struct StarLoomChannelRuntime {
  bool running = false;
  bool hasPendingSample = false;
  uint8_t midiNote = 0;
  int32_t pitchOffset = 0;
  AudioSample pendingSample = 0;
  braids::MacroOscillator oscillators[kOscillatorCount];
  braids::MacroOscillatorShape oscillatorShapes[kOscillatorCount] = {
      braids::MACRO_OSC_SHAPE_CSAW, braids::MACRO_OSC_SHAPE_CSAW};
};

struct StarLoomSharedRuntime {
  bool initialized = false;
  braids::SignatureWaveshaper waveshaper;
};

enum CommandRampIndex : size_t {
  Timbre1Ramp,
  Timbre2Ramp,
  Color1Ramp,
  Color2Ramp,
  Tune1Ramp,
  Tune2Ramp,
  MixRamp,
};

int DecodeSignedByte(uint8_t value) {
  return value < 0x80 ? static_cast<int>(value)
                      : static_cast<int>(value) - 0x100;
}

float GetCommandValue(const StarLoomCommandRamp &ramp, const Variable &base) {
  return ramp.stepsRemaining != 0.0f ? ramp.current
                                     : static_cast<float>(base.GetInt());
}

void AdvanceCommandRamp(StarLoomCommandRamp &ramp, float sequencerSteps) {
  if (ramp.stepsRemaining <= 0.0f || sequencerSteps <= 0.0f) {
    return;
  }
  const float advance = std::min(sequencerSteps, ramp.stepsRemaining);
  const float fraction = advance / ramp.stepsRemaining;
  ramp.current += (ramp.target - ramp.current) * fraction;
  ramp.stepsRemaining -= advance;
  if (ramp.stepsRemaining <= 1.0e-6f) {
    ramp.current = ramp.target;
    ramp.stepsRemaining = -1.0f;
  }
}

StarLoomChannelRuntime runtime_[SONG_CHANNEL_COUNT];
StarLoomSharedRuntime sharedRuntime_;
#if defined(ADV) && !defined(HOST_TEST) && !defined(WASM)
__attribute__((section(".RAM_D2"), aligned(4)))
#endif
StarLoomCommandRamp commandRamps_[SONG_CHANNEL_COUNT][kCommandRampCount] = {};
#if defined(ADV) && !defined(HOST_TEST) && !defined(WASM)
__attribute__((section(".RAM_EXT")))
#endif
alignas(32) braids::DigitalOscillatorDelayLines
    delayLines_[SONG_CHANNEL_COUNT][kOscillatorCount];

StarLoomChannelRuntime &GetRuntime(int channel) { return runtime_[channel]; }

void InitSharedRuntimeIfNeeded() {
  if (sharedRuntime_.initialized) {
    return;
  }

  System *system = System::GetInstance();
  const uint32_t seed = system != nullptr ? system->GetDeviceIdentitySeed()
                                          : kFallbackDeviceIdentitySeed;
  sharedRuntime_.waveshaper.Init(seed);

  for (size_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    StarLoomChannelRuntime &runtime = runtime_[channel];
    for (size_t oscillator = 0; oscillator < kOscillatorCount; ++oscillator) {
      runtime.oscillators[oscillator].AttachDelayLines(
          &delayLines_[channel][oscillator]);
      runtime.oscillators[oscillator].Init();
    }
  }
  sharedRuntime_.initialized = true;
}

} // namespace

StarLoomInstrument::StarLoomInstrument()
    : I_Instrument(&variables_),
      shape_(FourCC::StarLoomInstrumentShape,
             StarLoomInstrumentShapes::kAllowedShapeNames,
             StarLoomInstrumentShapes::kAllowedShapeCount, 0),
      timbre_(FourCC::StarLoomInstrumentTimbre, 0x7f),
      color_(FourCC::StarLoomInstrumentColor, 0x7f),
      signature_(FourCC::StarLoomInstrumentSignature, kSignatureNames,
                 sizeof(kSignatureNames) / sizeof(kSignatureNames[0]), 0),
      mix_(FourCC::StarLoomInstrumentMix, 0),
      oscillator1PitchOffset_(FourCC::StarLoomInstrumentOscillator1PitchOffset,
                              0),
      oscillator2Shape_(FourCC::StarLoomInstrumentOscillator2Shape,
                        StarLoomInstrumentShapes::kAllowedShapeNames,
                        StarLoomInstrumentShapes::kAllowedShapeCount, 0),
      oscillator2Timbre_(FourCC::StarLoomInstrumentOscillator2Timbre, 0x7f),
      oscillator2Color_(FourCC::StarLoomInstrumentOscillator2Color, 0x7f),
      oscillator2PitchOffset_(FourCC::StarLoomInstrumentOscillator2PitchOffset,
                              0),
      interactionMode_(
          FourCC::StarLoomInstrumentInteractionMode, kInteractionModeNames,
          StarLoomInstrumentDSP::kInteractionModeCount,
          static_cast<int>(StarLoomInstrumentDSP::InteractionMode::Mix)),
      table_(FourCC::StarLoomInstrumentTable, VAR_OFF),
      tableAuto_(FourCC::StarLoomInstrumentTableAutomation, false) {

  variables_.insert(variables_.end(), &shape_);
  variables_.insert(variables_.end(), &timbre_);
  variables_.insert(variables_.end(), &color_);
  variables_.insert(variables_.end(), &signature_);
  variables_.insert(variables_.end(), &mix_);
  variables_.insert(variables_.end(), &oscillator1PitchOffset_);
  variables_.insert(variables_.end(), &oscillator2Shape_);
  variables_.insert(variables_.end(), &oscillator2Timbre_);
  variables_.insert(variables_.end(), &oscillator2Color_);
  variables_.insert(variables_.end(), &oscillator2PitchOffset_);
  variables_.insert(variables_.end(), &interactionMode_);
  variables_.insert(variables_.end(), &table_);
  variables_.insert(variables_.end(), &tableAuto_);
}

StarLoomInstrument::~StarLoomInstrument() {}

bool StarLoomInstrument::Init() {
  InitSharedRuntimeIfNeeded();
  tableState_.Reset();
  return true;
}

void StarLoomInstrument::OnStart() { tableState_.Reset(); };

bool StarLoomInstrument::Start(int channel, unsigned char midinote,
                               InstrumentStartMode mode) {
  StarLoomChannelRuntime &runtime = GetRuntime(channel);
  if (mode == InstrumentStartMode::NewNote) {
    for (StarLoomCommandRamp &ramp : commandRamps_[channel]) {
      ramp = {};
    }
  }
  if (mode != InstrumentStartMode::TiedNote) {
    runtime.oscillators[0].Init();
    runtime.oscillators[1].Init();
  }

  runtime.oscillatorShapes[0] =
      StarLoomInstrumentShapes::ShapeForSelection(shape_.GetInt());
  runtime.oscillatorShapes[1] =
      StarLoomInstrumentShapes::ShapeForSelection(oscillator2Shape_.GetInt());

  runtime.running = true;
  runtime.hasPendingSample = false;
  runtime.midiNote = midinote;

  // used convention: pT midi #81 == A4 vs braids #69
  runtime.oscillators[0].set_pitch(StarLoomInstrumentDSP::CalculatePitch(
      midinote, oscillator1PitchOffset_.GetInt(), runtime.pitchOffset));
  runtime.oscillators[1].set_pitch(StarLoomInstrumentDSP::CalculatePitch(
      midinote, oscillator2PitchOffset_.GetInt(), runtime.pitchOffset));

  // start is a trigger, reset data
  runtime.oscillators[0].Strike();
  runtime.oscillators[1].Strike();
  return true;
}

void StarLoomInstrument::Stop(int channel) {
  GetRuntime(channel).running = false;
}

void StarLoomInstrument::SetPitchMultiplier(int channel, float multiplier) {
  StarLoomChannelRuntime &runtime = GetRuntime(channel);
  runtime.pitchOffset =
      StarLoomInstrumentDSP::PitchMultiplierToPitchUnits(multiplier);
}

// Size in samples
bool StarLoomInstrument::Render(int channel, AudioSample *buffer, int size,
                                bool updateTick) {
  (void)updateTick;
  StarLoomChannelRuntime &runtime = GetRuntime(channel);
  if (!runtime.running) {
    return false;
  }

  memset(buffer, 0, size * sizeof(AudioSample));

  uint8_t sync_buffer[kBraidsBlockSize] = {};
  int16_t oscillator1Buffer[kBraidsBlockSize];
  int16_t oscillator2Buffer[kBraidsBlockSize];
  const uint16_t signature = signature_.GetInt() * signature_.GetInt() * 4095;
  const auto interactionMode =
      static_cast<StarLoomInstrumentDSP::InteractionMode>(
          interactionMode_.GetInt());
  SyncMaster *syncMaster = SyncMaster::GetInstance();
  const float stepSampleCount =
      syncMaster ? syncMaster->GetStepSampleCount() : 0.0f;

  size_t rendered = 0;
  if (runtime.hasPendingSample && size > 0) {
    buffer[rendered++] = runtime.pendingSample;
    runtime.hasPendingSample = false;
  }
  while (rendered < static_cast<size_t>(size)) {
    const size_t outputBlockSize =
        static_cast<size_t>(size) - rendered < kBraidsBlockSize
            ? static_cast<size_t>(size) - rendered
            : kBraidsBlockSize;
    // Several Braids algorithms generate pairs and require an even size.
    const size_t renderBlockSize =
        StarLoomInstrumentDSP::BraidsRenderSize(outputBlockSize);
    const float timbre1 =
        GetCommandValue(commandRamps_[channel][Timbre1Ramp], timbre_);
    const float timbre2 = GetCommandValue(commandRamps_[channel][Timbre2Ramp],
                                          oscillator2Timbre_);
    const float color1 =
        GetCommandValue(commandRamps_[channel][Color1Ramp], color_);
    const float color2 =
        GetCommandValue(commandRamps_[channel][Color2Ramp], oscillator2Color_);
    const float tune1 = GetCommandValue(commandRamps_[channel][Tune1Ramp],
                                        oscillator1PitchOffset_);
    const float tune2 = GetCommandValue(commandRamps_[channel][Tune2Ramp],
                                        oscillator2PitchOffset_);
    const float mixValue =
        GetCommandValue(commandRamps_[channel][MixRamp], mix_);
    const int32_t oscillator1Level =
        StarLoomInstrumentDSP::Oscillator1LevelFromMix(
            static_cast<int32_t>(std::lround(mixValue)));
    const int32_t oscillator2Level =
        StarLoomInstrumentDSP::Oscillator2LevelFromMix(
            static_cast<int32_t>(std::lround(mixValue)));
    const bool mixRampActive =
        commandRamps_[channel][MixRamp].stepsRemaining > 0.0f;
    // During a Mix ramp, render both oscillators so a previously muted voice
    // can enter the crossfade without waiting for its next note trigger.
    const bool renderOscillator1 = oscillator1Level > 0 || mixRampActive;
    const bool renderOscillator2 = oscillator2Level > 0 || mixRampActive;

    runtime.oscillators[0].set_shape(runtime.oscillatorShapes[0]);
    runtime.oscillators[0].set_parameters(
        static_cast<int32_t>(std::lround(timbre1 * 128.0f)),
        static_cast<int32_t>(std::lround(color1 * 128.0f)));
    runtime.oscillators[1].set_shape(runtime.oscillatorShapes[1]);
    runtime.oscillators[1].set_parameters(
        static_cast<int32_t>(std::lround(timbre2 * 128.0f)),
        static_cast<int32_t>(std::lround(color2 * 128.0f)));
    runtime.oscillators[0].set_pitch(StarLoomInstrumentDSP::CalculatePitch(
        runtime.midiNote, static_cast<int32_t>(std::lround(tune1)),
        runtime.pitchOffset));
    runtime.oscillators[1].set_pitch(StarLoomInstrumentDSP::CalculatePitch(
        runtime.midiNote, static_cast<int32_t>(std::lround(tune2)),
        runtime.pitchOffset));
    if (renderOscillator1) {
      runtime.oscillators[0].Render(sync_buffer, oscillator1Buffer,
                                    renderBlockSize);
    }
    if (renderOscillator2) {
      runtime.oscillators[1].Render(sync_buffer, oscillator2Buffer,
                                    renderBlockSize);
    }

    for (size_t j = 0; j < renderBlockSize; ++j) {
      int16_t oscillator1 = 0;
      if (renderOscillator1) {
        if (signature == 0) {
          oscillator1 = StarLoomInstrumentDSP::ApplyDisabledSignature(
              oscillator1Buffer[j]);
        } else {
          const int16_t warped =
              sharedRuntime_.waveshaper.Transform(oscillator1Buffer[j]);
          oscillator1 = braids::Mix(oscillator1Buffer[j], warped, signature);
        }
      }
      int16_t oscillator2 = 0;
      if (renderOscillator2) {
        if (signature == 0) {
          oscillator2 = StarLoomInstrumentDSP::ApplyDisabledSignature(
              oscillator2Buffer[j]);
        } else {
          const int16_t warped =
              sharedRuntime_.waveshaper.Transform(oscillator2Buffer[j]);
          oscillator2 = braids::Mix(oscillator2Buffer[j], warped, signature);
        }
      }
      const int32_t mix = StarLoomInstrumentDSP::ApplyInteraction(
                              oscillator1, oscillator2, oscillator1Level,
                              oscillator2Level, interactionMode) /
                          4;
      const AudioSample output = static_cast<AudioSample>(mix) / kAudioPcmScale;
      if (j < outputBlockSize) {
        buffer[rendered + j] = output;
      } else {
        runtime.pendingSample = output;
        runtime.hasPendingSample = true;
      }
    }

    rendered += outputBlockSize;
    if (stepSampleCount > 0.0f) {
      const float sequencerSteps =
          static_cast<float>(outputBlockSize) / stepSampleCount;
      for (StarLoomCommandRamp &ramp : commandRamps_[channel]) {
        AdvanceCommandRamp(ramp, sequencerSteps);
      }
    }
  }
  return true;
};

AudioBufferLayout StarLoomInstrument::GetAudioBufferLayout(int channel) const {
  (void)channel;
  return AudioBufferLayout::MonoPacked;
}

bool StarLoomInstrument::IsInitialized() { /*return (source_ != 0); */
  return true;
};

bool StarLoomInstrument::ShouldAuditionParameterChange(FourCC parameter) const {
  switch (parameter) {
  case FourCC::StarLoomInstrumentMix:
  case FourCC::StarLoomInstrumentInteractionMode:
  case FourCC::StarLoomInstrumentSignature:
  case FourCC::StarLoomInstrumentShape:
  case FourCC::StarLoomInstrumentOscillator1PitchOffset:
  case FourCC::StarLoomInstrumentTimbre:
  case FourCC::StarLoomInstrumentColor:
  case FourCC::StarLoomInstrumentOscillator2Shape:
  case FourCC::StarLoomInstrumentOscillator2PitchOffset:
  case FourCC::StarLoomInstrumentOscillator2Timbre:
  case FourCC::StarLoomInstrumentOscillator2Color:
    return true;
  default:
    return false;
  }
}

void StarLoomInstrument::Update(Observable &o, I_ObservableData *d){};

void StarLoomInstrument::ProcessCommand(int channel, TrackerCommand cc,
                                        ushort value) {
  if (cc == TrackerCommand::InstrumentCommandTable) {
    table_.SetInt(value);
    return;
  }

  size_t rampIndex = kCommandRampCount;
  Variable *base = nullptr;
  int32_t minimum = 0;
  int32_t maximum = 0xFF;
  switch (cc) {
  case TrackerCommand::InstrumentCommandStarLoomTimbre1:
    rampIndex = Timbre1Ramp;
    base = &timbre_;
    break;
  case TrackerCommand::InstrumentCommandStarLoomTimbre2:
    rampIndex = Timbre2Ramp;
    base = &oscillator2Timbre_;
    break;
  case TrackerCommand::InstrumentCommandStarLoomColor1:
    rampIndex = Color1Ramp;
    base = &color_;
    break;
  case TrackerCommand::InstrumentCommandStarLoomColor2:
    rampIndex = Color2Ramp;
    base = &oscillator2Color_;
    break;
  case TrackerCommand::InstrumentCommandStarLoomTune1:
    rampIndex = Tune1Ramp;
    base = &oscillator1PitchOffset_;
    minimum = PitchOffset::MinimumCents;
    maximum = PitchOffset::MaximumCents;
    break;
  case TrackerCommand::InstrumentCommandStarLoomTune2:
    rampIndex = Tune2Ramp;
    base = &oscillator2PitchOffset_;
    minimum = PitchOffset::MinimumCents;
    maximum = PitchOffset::MaximumCents;
    break;
  case TrackerCommand::InstrumentCommandStarLoomMix:
    rampIndex = MixRamp;
    base = &mix_;
    minimum = -StarLoomInstrumentDSP::kLevelMax;
    maximum = StarLoomInstrumentDSP::kLevelMax;
    break;
  default:
    return;
  }

  const uint8_t durationTicks = static_cast<uint8_t>(value >> 8);
  const uint8_t encodedOffset = static_cast<uint8_t>(value & 0xFF);
  int offset = DecodeSignedByte(encodedOffset);
  if (rampIndex == Tune1Ramp || rampIndex == Tune2Ramp) {
    offset = std::clamp(offset, -127, 127);
  }
  StarLoomCommandRamp &ramp = commandRamps_[channel][rampIndex];
  const float start = ramp.stepsRemaining != 0.0f
                          ? ramp.current
                          : static_cast<float>(base->GetInt());
  const float target = static_cast<float>(
      std::clamp(base->GetInt() + offset, static_cast<int>(minimum),
                 static_cast<int>(maximum)));
  const float durationSteps =
      static_cast<float>(durationTicks) / SyncMaster::SlicesPerStep;
  ramp.current = start;
  ramp.target = target;
  const bool rampActive = durationSteps > 0.0f && start != target;
  ramp.stepsRemaining = rampActive ? durationSteps : -1.0f;
  if (!rampActive) {
    ramp.current = target;
  }
};

bool StarLoomInstrument::IsEmpty() { return false; };

int StarLoomInstrument::GetTable() { return table_.GetInt(); };

bool StarLoomInstrument::GetTableAutomation() { return tableAuto_.GetBool(); };

void StarLoomInstrument::GetTableState(TableSaveState &state) {
  state = tableState_;
};

void StarLoomInstrument::SetTableState(TableSaveState &state) {
  tableState_ = state;
};
