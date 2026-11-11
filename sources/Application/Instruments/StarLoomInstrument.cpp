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

StarLoomChannelRuntime runtime_[SONG_CHANNEL_COUNT];
StarLoomSharedRuntime sharedRuntime_;
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

  runtime.oscillators[0].set_shape(runtime.oscillatorShapes[0]);
  runtime.oscillators[0].set_parameters(
      timbre_.GetInt() * 128, color_.GetInt() * 128); // timbre and color
  runtime.oscillators[1].set_shape(runtime.oscillatorShapes[1]);
  runtime.oscillators[1].set_parameters(oscillator2Timbre_.GetInt() * 128,
                                        oscillator2Color_.GetInt() * 128);
  runtime.oscillators[0].set_pitch(StarLoomInstrumentDSP::CalculatePitch(
      runtime.midiNote, oscillator1PitchOffset_.GetInt(), runtime.pitchOffset));
  runtime.oscillators[1].set_pitch(StarLoomInstrumentDSP::CalculatePitch(
      runtime.midiNote, oscillator2PitchOffset_.GetInt(), runtime.pitchOffset));

  uint8_t sync_buffer[kBraidsBlockSize] = {};
  int16_t oscillator1Buffer[kBraidsBlockSize];
  int16_t oscillator2Buffer[kBraidsBlockSize];
  const uint16_t signature = signature_.GetInt() * signature_.GetInt() * 4095;
  const int32_t oscillator1Level =
      StarLoomInstrumentDSP::Oscillator1LevelFromMix(mix_.GetInt());
  const int32_t oscillator2Level =
      StarLoomInstrumentDSP::Oscillator2LevelFromMix(mix_.GetInt());
  const auto interactionMode =
      static_cast<StarLoomInstrumentDSP::InteractionMode>(
          interactionMode_.GetInt());
  // Skipping muted oscillators saves CPU and avoids consuming Braids' shared
  // random stream, which keeps legacy single-oscillator patches stable.
  const bool renderOscillator1 = oscillator1Level > 0;
  const bool renderOscillator2 = oscillator2Level > 0;

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
  (void)channel;
  if (cc == TrackerCommand::InstrumentCommandTable) {
    table_.SetInt(value);
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
