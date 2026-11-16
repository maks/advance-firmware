/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "AudioVoice.h"
#include "Application/Player/SyncMaster.h"
#include "AudioVoiceFilterGLUT.h"
#include "Services/Audio/AudioDriver.h"
#include "System/Profiler/DWTProfiler.h"
#include "config/MemorySections.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
constexpr int kEngineSampleRate = 44100;
constexpr int kLFOControlQuantumSamples = 24; // ~500us
constexpr float kMinFilterFrequencyHz = 20.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEnvelopeEpsilon = 1.0e-6f;
constexpr int kMaxTriggerLengthSlices = 96;
constexpr uint32_t kProfileComponentCount =
    static_cast<uint32_t>(VoiceProfileComponent::Count);
constexpr uint32_t kVoicesBudgetPctX1000 = 50000U;
constexpr uint32_t kChannelBudgetPctX1000 =
    ComputeDWTBudgetSharePctX1000(kVoicesBudgetPctX1000, SONG_CHANNEL_COUNT);
constexpr uint32_t kInputEffectTotalBudgetPctX1000 = 4000U;
constexpr uint32_t kInputEffectBudgetPctX1000 = ComputeDWTBudgetSharePctX1000(
    kInputEffectTotalBudgetPctX1000, SONG_CHANNEL_COUNT);

DWTProfileStats gVoiceComponentProfiles[kProfileComponentCount]
                                       [SONG_CHANNEL_COUNT] = {};
DWTProfileStats gVoiceChannelTotalProfiles[SONG_CHANNEL_COUNT] = {};
DWTProfileStats gVoicesTotalProfile = {};
DWTProfileMatrixRow gVoiceProfileRows[kProfileComponentCount] = {
    {.name_ = "Voice source"},
    {.name_ = "Voice BW"},
    {.name_ = "Voice filter"},
    {.name_ = "Voice AMP"},
    {.name_ = "Voice PAN"},
    {.name_ = "Chan LFO"},
    {.name_ = "ifx1", .budgetPctX1000_ = kInputEffectBudgetPctX1000},
    {.name_ = "ifx2", .budgetPctX1000_ = kInputEffectBudgetPctX1000},
    {.name_ = "Player control"},
    {.name_ = "wet/dry"},
};
DWTProfileMatrix gVoiceProfileMatrix = {
    .rows_ = gVoiceProfileRows,
    .rowCount_ = kProfileComponentCount,
    .columnCount_ = SONG_CHANNEL_COUNT,
    .channelBudgetPctX1000_ = kChannelBudgetPctX1000,
    .voicesBudgetPctX1000_ = kVoicesBudgetPctX1000,
};

inline float lookupFilterG(float normalized) {
  const float position =
      std::clamp(normalized, 0.0f, 1.0f) *
      static_cast<float>(AudioVoiceFilterGLUT::kFilterGLUTSize);
  const size_t index = std::min(static_cast<size_t>(position),
                                AudioVoiceFilterGLUT::kFilterGLUTSize - 1);
  const float fraction = position - static_cast<float>(index);
  return AudioVoiceFilterGLUT::kFilterGValues[index] +
         (AudioVoiceFilterGLUT::kFilterGValues[index + 1] -
          AudioVoiceFilterGLUT::kFilterGValues[index]) *
             fraction;
}

struct StandardFilterCoefficients {
  float a1_;
  float a2_;
  float a3_;
};

PICOTRACKER_FAST_CODE("audio_voice_standard_filter_coefficients")
inline StandardFilterCoefficients
calculateStandardFilterCoefficients(float cutoff, float k) {
  // The constant table maps normalized cutoff through the exact exponential
  // frequency curve and tan(pi * f / fs) TPT coefficient transform.
  const float g = lookupFilterG(cutoff);
  const float a1 = 1.0f / (1.0f + g * (g + k));
  const float a2 = g * a1;
  return {a1, a2, g * a2};
}

PICOTRACKER_FAST_CODE("audio_voice_standard_filter_sample")
inline AudioSample
processStandardFilterSample(AudioSample inputSample,
                            const StandardFilterCoefficients &coefficients,
                            float inputWeight, float bandpassWeight,
                            float lowpassWeight, float &ic1eq, float &ic2eq) {
  const float v3 = inputSample - ic2eq;
  const float v1 = coefficients.a1_ * ic1eq + coefficients.a2_ * v3;
  const float v2 = ic2eq + coefficients.a2_ * ic1eq + coefficients.a3_ * v3;
  ic1eq = 2.0f * v1 - ic1eq;
  ic2eq = 2.0f * v2 - ic2eq;

  return inputWeight * inputSample + bandpassWeight * v1 + lowpassWeight * v2;
}

void registerVoiceProfiles() {
  static bool registered = false;
  if (registered) {
    return;
  }

  for (uint32_t row = 0; row < kProfileComponentCount; ++row) {
    for (uint32_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
      DWTProfileStats &stats = gVoiceComponentProfiles[row][channel];
      stats.printIndividually_ = false;
      gVoiceProfileRows[row].columns_[channel] = &stats;
    }
  }
  for (uint32_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    gVoiceChannelTotalProfiles[channel].printIndividually_ = false;
    gVoiceProfileMatrix.channelTotals_[channel] =
        &gVoiceChannelTotalProfiles[channel];
  }
  gVoicesTotalProfile.printIndividually_ = false;
  gVoiceProfileMatrix.voicesTotal_ = &gVoicesTotalProfile;
  RegisterDWTProfileMatrix(&gVoiceProfileMatrix);
  registered = true;
}

inline int clampPan(int value) { return std::clamp(value, 0, 0xFE); }

inline float byteToUnit(int value) {
  return std::clamp(value, 0, 0xFF) / 255.0f;
}

inline float bipolarDepth(int value) {
  return std::clamp(value, -64, 64) / 64.0f;
}

inline float mapCutoffHz(int value) {
  return kMinFilterFrequencyHz *
         std::pow(std::min(kEngineSampleRate * 0.45f, 18000.0f) /
                      kMinFilterFrequencyHz,
                  byteToUnit(value));
}

inline float maxCutoffHz() {
  return std::min(kEngineSampleRate * 0.45f, 18000.0f);
}

inline float onePoleAlpha(float cutoffHz) {
  if (cutoffHz <= 0.0f) {
    return 0.0f;
  }

  const float normalized =
      std::exp((-2.0f * kPi * cutoffHz) / kEngineSampleRate);
  return std::clamp(1.0f - normalized, 0.0f, 1.0f);
}

inline float semitoneRatio(float semitones) {
  return std::pow(2.0f, semitones / 12.0f);
}

inline float advanceFixedDurationRamp(float sequencerSteps,
                                      float &stepsRemaining, bool &active) {
  if (!active) {
    return 0.0f;
  }
  if (stepsRemaining <= 0.0f) {
    stepsRemaining = 0.0f;
    active = false;
    return 1.0f;
  }
  if (sequencerSteps <= 0.0f) {
    return 0.0f;
  }
  if (sequencerSteps >= stepsRemaining) {
    stepsRemaining = 0.0f;
    active = false;
    return 1.0f;
  }

  const float progress = sequencerSteps / stepsRemaining;
  stepsRemaining -= sequencerSteps;
  return progress;
}

inline bool advanceFixedDurationLinearRamp(float sequencerSteps, float target,
                                           float &current,
                                           float &stepsRemaining,
                                           bool &active) {
  const float previous = current;
  const float progress =
      advanceFixedDurationRamp(sequencerSteps, stepsRemaining, active);
  if (progress >= 1.0f) {
    current = target;
  } else if (progress > 0.0f) {
    current += (target - current) * progress;
  }
  return current != previous;
}

inline float samplesToSequencerSteps(int samplecount, float stepSampleCount) {
  if (samplecount <= 0 || stepSampleCount <= 0.0f) {
    return 0.0f;
  }
  return static_cast<float>(samplecount) / stepSampleCount;
}

inline float mapResonanceQ(int value) {
  const float norm = byteToUnit(value);
  return 0.707f + (norm * norm) * 15.0f;
}

inline AudioSample panGainLeft(int pan) {
  const float norm = std::clamp(pan / 254.0f, 0.0f, 1.0f);
  const float gain = (norm <= 0.5f) ? 1.0f : (2.0f * (1.0f - norm));
  return std::clamp(gain, 0.0f, 1.0f);
}

inline AudioSample panGainRight(int pan) {
  const float norm = std::clamp(pan / 254.0f, 0.0f, 1.0f);
  const float gain = (norm >= 0.5f) ? 1.0f : (2.0f * norm);
  return std::clamp(gain, 0.0f, 1.0f);
}

} // namespace

DWTProfileStats *GetVoiceProfileStats(VoiceProfileComponent component,
                                      uint32_t channel) {
  const uint32_t row = static_cast<uint32_t>(component);
  if (row >= kProfileComponentCount || channel >= SONG_CHANNEL_COUNT) {
    return nullptr;
  }
  return &gVoiceComponentProfiles[row][channel];
}

DWTProfileStats *GetVoiceChannelTotalProfileStats(uint32_t channel) {
  if (channel >= SONG_CHANNEL_COUNT) {
    return nullptr;
  }
  return &gVoiceChannelTotalProfiles[channel];
}

DWTProfileStats *GetVoicesTotalProfileStats() { return &gVoicesTotalProfile; }

bool AudioVoice::FixedDurationLinearPitchRampState::AdvanceControl(
    float sequencerSteps) {
  return advanceFixedDurationLinearRamp(sequencerSteps, target_, current_,
                                        stepsRemaining_, active_);
}

void AudioVoice::FixedDurationLinearPitchRampState::Reset() {
  current_ = 1.0f;
  target_ = 1.0f;
  stepsRemaining_ = 0.0f;
  active_ = false;
}

bool AudioVoice::FixedDurationLogPitchRampState::AdvanceControl(
    float sequencerSteps) {
  const float previous = current_;
  const float progress =
      advanceFixedDurationRamp(sequencerSteps, stepsRemaining_, active_);
  if (progress >= 1.0f) {
    current_ = target_;
  } else if (progress > 0.0f) {
    current_ *= std::pow(target_ / current_, progress);
  }
  return current_ != previous;
}

void AudioVoice::FixedDurationLogPitchRampState::Reset() {
  current_ = 1.0f;
  target_ = 1.0f;
  stepsRemaining_ = 0.0f;
  active_ = false;
}

void AudioVoice::ArpeggiatorState::Configure(uint16_t value) {
  uint16_t arp = value;
  uint8_t length = 0;
  values_[0] = 0;
  for (uint8_t index = 0; index < 4; ++index) {
    values_[4 - index] = arp & 0xF;
    if (values_[4 - index] != 0 && length == 0) {
      length = 5 - index;
    }
    arp >>= 4;
  }
  length_ = length;
  position_ = 0;
  current_ = 1.0f;
  active_ = true;
}

bool AudioVoice::ArpeggiatorState::AdvanceTick() {
  if (!active_ || length_ == 0) {
    return false;
  }

  const float previous = current_;
  position_++;
  if (position_ > length_) {
    position_ = 0;
  }
  current_ = semitoneRatio(static_cast<float>(values_[position_]));
  return current_ != previous;
}

void AudioVoice::ArpeggiatorState::Reset() {
  for (uint8_t &value : values_) {
    value = 0;
  }
  position_ = 0;
  length_ = 0;
  current_ = 1.0f;
  active_ = false;
}

AudioVoice::AudioVoice(int channel)
    : channel_(channel), source_(nullptr), currentNote_(-1), previousNote_(-1),
      legatoNotePending_(false), lfoBank_(channel),
      triggerReleaseSlicesRemaining_(0), releasePending_(false) {
  registerVoiceProfiles();
  clearInsertEffects();
  resetCommandState();
  resetStageState();
}

bool AudioVoice::StartInstrument(I_Instrument *instrument, unsigned char note,
                                 InstrumentStartMode mode, uint8_t velocity) {
  if (mode == InstrumentStartMode::Retrigger && source_ != instrument) {
    mode = InstrumentStartMode::NewNote;
  }
  const bool continueVoice =
      source_ == instrument && mode == InstrumentStartMode::TiedNote;
  const bool smoothSampleReplacement = source_ != nullptr &&
                                       instrument != nullptr &&
                                       instrument->GetType() == IT_MSAMPLER &&
                                       mode != InstrumentStartMode::TiedNote;
  const AudioSample precedingLeft = lastSampleLeft_;
  const AudioSample precedingRight = lastSampleRight_;
  const int16_t precedingNote = currentNote_;

  if (source_ && !continueVoice && mode != InstrumentStartMode::Retrigger) {
    StopInstrument();
  }

  if (!instrument) {
    return false;
  }

  if (mode == InstrumentStartMode::Retrigger) {
    instrument->Stop(channel_);
  }
  const bool started =
      instrument->StartWithVelocity(channel_, note, velocity, mode);
  if (!started) {
    if (continueVoice || mode == InstrumentStartMode::Retrigger) {
      StopInstrument();
    } else {
      source_ = nullptr;
    }
    return false;
  }

  source_ = instrument;
  if (continueVoice && precedingNote >= 0) {
    previousNote_ = precedingNote;
    currentNote_ = note;
    legatoNotePending_ = true;
  } else {
    currentNote_ = note;
    previousNote_ = note;
    legatoNotePending_ = false;
  }
  lfoBank_.SetInstrument(source_);
  lfoBank_.OnNoteStart(mode != InstrumentStartMode::TiedNote);
  refreshParameterRefs();
  updateFilterEnvelopeParameters();
  updateAmpEnvelopeParameters();

  if (!continueVoice) {
    transitionFramesRemaining_ = 0;
    // IRT deliberately bypasses the entire command reset lifecycle. New
    // commands participate automatically; never snapshot selected commands.
    if (mode != InstrumentStartMode::Retrigger) {
      resetCommandState();
    }
    resetTriggerRelease();
    releasePending_ = false;
    ampEnvelope_.NoteOn();
    filterEnvelope_.NoteOn();
  }

  if (mode != InstrumentStartMode::TiedNote) {
    resetStageState();
  }
  pitchMultiplierApplied_ = false;
  if (InstrumentTypeUsesModernAudioVoice(source_->GetType())) {
    applyPitchMultiplier();
  }
  if (smoothSampleReplacement) {
    transitionLeft_ = precedingLeft;
    transitionRight_ = precedingRight;
    transitionFramesRemaining_ = sampleTransitionFrames;
  }
  return true;
}

bool AudioVoice::ProcessCommand(TrackerCommand command, ushort value) {
  if (!source_) {
    return false;
  }

  if (processPitchCommand(command, value)) {
    return true;
  }

  switch (command) {
  case TrackerCommand::InstrumentCommandVolume:
    setCommandRamp(ampCommand_, static_cast<float>(value & 0xFF),
                   static_cast<float>(value >> 8), params_.amp_, 0xFF);
    return true;

  case TrackerCommand::InstrumentCommandPan:
    setCommandRamp(panCommand_, static_cast<float>(value & 0xFF),
                   static_cast<float>(value >> 8), params_.pan_, 0x7F, true);
    return true;

  case TrackerCommand::InstrumentCommandFilterCut:
    setCommandRamp(filterCutoffCommand_, static_cast<float>(value & 0xFF),
                   static_cast<float>(value >> 8), params_.filterCutoff_, 0xFF);
    return true;

  case TrackerCommand::InstrumentCommandFilterResonance:
    setCommandRamp(filterResonanceCommand_, static_cast<float>(value & 0xFF),
                   static_cast<float>(value >> 8), params_.filterResonance_, 0);
    return true;

  case TrackerCommand::InstrumentCommandLowPassFilter:
    setCommandImmediate(filterCutoffCommand_, static_cast<float>(value >> 8));
    setCommandImmediate(filterResonanceCommand_,
                        static_cast<float>(value & 0xFF));
    cancelCommandRamp(filterCutoffCommand_);
    cancelCommandRamp(filterResonanceCommand_);
    return true;

  case TrackerCommand::InstrumentCommandGateLength:
    gateCommandSlicesRemaining_ = static_cast<uint16_t>((value & 0xFF) + 1);
    gateCommandActive_ = true;
    return true;

  default:
    return false;
  }
}

bool AudioVoice::processPitchCommand(TrackerCommand command, ushort value) {
  switch (command) {
  case TrackerCommand::InstrumentCommandArpeggiator: {
    ArpeggiatorState &arpeggiator = pitchCommand_.arpeggiator_;
    if (!registerPitchUpdater(arpeggiator)) {
      return false;
    }
    arpeggiator.Configure(value);
    markPitchMultiplierDirty();
    applyPitchMultiplier();
    return true;
  }

  case TrackerCommand::InstrumentCommandPitchSlide: {
    const int8_t pitch = static_cast<int8_t>(value & 0xFF);
    const float speed = static_cast<float>(value >> 8);
    FixedDurationLinearPitchRampState &ramp = pitchCommand_.pitchSlide_;
    if (!registerPitchUpdater(ramp)) {
      return false;
    }
    ramp.target_ = semitoneRatio(static_cast<float>(pitch));
    ramp.stepsRemaining_ = commandSpeedDurationSteps(speed);
    ramp.active_ = speed != 0.0f;
    if (!ramp.active_) {
      ramp.current_ = ramp.target_;
      markPitchMultiplierDirty();
    }
    return true;
  }

  case TrackerCommand::InstrumentCommandLegato: {
    // LEG only describes the duration of a tied-note transition. Its low byte
    // is deliberately ignored; the two notes provide both endpoints.
    if (!legatoNotePending_) {
      return true;
    }

    const int16_t pitch = previousNote_ - currentNote_;
    const float speed = static_cast<float>(value >> 8);
    FixedDurationLogPitchRampState &ramp = pitchCommand_.legato_;
    if (!registerPitchUpdater(ramp)) {
      return false;
    }
    legatoNotePending_ = false;
    // The instrument has already switched to the tied note's base pitch.
    // Translate the existing LEG contribution into that new reference frame
    // so retargeting an unfinished glide does not jump to the previous note.
    ramp.current_ *= semitoneRatio(static_cast<float>(pitch));
    ramp.target_ = 1.0f;
    ramp.stepsRemaining_ = commandSpeedDurationSteps(speed);
    ramp.active_ = speed != 0.0f;
    if (!ramp.active_) {
      ramp.current_ = ramp.target_;
    }
    markPitchMultiplierDirty();
    return true;
  }

  case TrackerCommand::InstrumentCommandPitchFineTune: {
    float semitones = (value & 0xFF) / 128.0f;
    if (semitones > 1.0f) {
      semitones -= 2.0f;
    }

    const float speed = static_cast<float>(value >> 8);
    FixedDurationLogPitchRampState &ramp = pitchCommand_.fineTune_;
    if (!registerPitchUpdater(ramp)) {
      return false;
    }
    ramp.target_ = semitoneRatio(semitones);
    ramp.stepsRemaining_ = commandSpeedDurationSteps(speed);
    ramp.active_ = speed != 0.0f;
    if (!ramp.active_) {
      ramp.current_ = ramp.target_;
      markPitchMultiplierDirty();
    }
    return true;
  }

  default:
    return false;
  }
}

bool AudioVoice::registerPitchUpdater(PitchUpdater &updater) {
  if (updater.registered_) {
    return true;
  }

  if (pitchCommand_.contributors_.full()) {
    return false;
  }

  pitchCommand_.contributors_.push_back(&updater);
  updater.registered_ = true;
  markPitchMultiplierDirty();
  return true;
}

void AudioVoice::resetPitchCommands() {
  for (PitchUpdater *updater : pitchCommand_.contributors_) {
    updater->Reset();
    updater->registered_ = false;
  }
  pitchCommand_.contributors_.clear();
  pitchCommand_.currentMultiplier_ = 1.0f;
  pitchCommand_.multiplierDirty_ = true;
}

void AudioVoice::advancePitchTick() {
  for (PitchUpdater *updater : pitchCommand_.contributors_) {
    if (updater->AdvanceTick()) {
      markPitchMultiplierDirty();
    }
  }
}

void AudioVoice::advancePitchRamps(float sequencerSteps) {
  for (PitchUpdater *updater : pitchCommand_.contributors_) {
    if (updater->AdvanceControl(sequencerSteps)) {
      markPitchMultiplierDirty();
    }
  }
}

int AudioVoice::getPitchRampCompletionSamples(float stepSampleCount) const {
  if (stepSampleCount <= 0.0f) {
    return 0;
  }

  float stepsRemaining = std::numeric_limits<float>::max();
  if (pitchCommand_.pitchSlide_.active_) {
    stepsRemaining =
        std::min(stepsRemaining, pitchCommand_.pitchSlide_.stepsRemaining_);
  }
  if (pitchCommand_.legato_.active_) {
    stepsRemaining =
        std::min(stepsRemaining, pitchCommand_.legato_.stepsRemaining_);
  }
  if (pitchCommand_.fineTune_.active_) {
    stepsRemaining =
        std::min(stepsRemaining, pitchCommand_.fineTune_.stepsRemaining_);
  }
  if (stepsRemaining == std::numeric_limits<float>::max()) {
    return 0;
  }

  return std::max(
      1, static_cast<int>(std::ceil(stepsRemaining * stepSampleCount)));
}

bool AudioVoice::hasActivePitchRamp() const {
  for (const PitchUpdater *updater : pitchCommand_.contributors_) {
    if (updater->HasActiveRamp()) {
      return true;
    }
  }
  return false;
}

void AudioVoice::markPitchMultiplierDirty() {
  pitchCommand_.multiplierDirty_ = true;
}

void AudioVoice::updatePitchMultiplier() {
  if (!pitchCommand_.multiplierDirty_) {
    return;
  }

  float multiplier = 1.0f;
  for (const PitchUpdater *updater : pitchCommand_.contributors_) {
    multiplier *= updater->GetCurrentRatio();
  }
  pitchCommand_.currentMultiplier_ = multiplier;
  pitchCommand_.multiplierDirty_ = false;
}

void AudioVoice::applyPitchMultiplier() {
  updatePitchMultiplier();
  const float multiplier = pitchCommand_.currentMultiplier_;
  if (pitchMultiplierApplied_ && multiplier == appliedPitchMultiplier_) {
    return;
  }

  source_->SetPitchMultiplier(channel_, multiplier);
  appliedPitchMultiplier_ = multiplier;
  pitchMultiplierApplied_ = true;
}

void AudioVoice::ReleaseInstrument() {
  if (!source_) {
    return;
  }
  triggerReleaseSlicesRemaining_ = 0;

  // Legacy sample instruments are a deliberate exception: they still flow
  // through AudioVoice ownership on the channel, but they do not use the voice
  // envelope/FX pipeline and should stop immediately on release.
  if (source_->GetType() == IT_SAMPLE) {
    StopInstrument();
    return;
  }

  if (releasePending_) {
    return;
  }

  source_->Release(channel_);
  releasePending_ = true;
  ampEnvelope_.NoteOff();
  filterEnvelope_.NoteOff();
}

void AudioVoice::StopInstrument() {
  lastSampleLeft_ = 0.0f;
  lastSampleRight_ = 0.0f;
  transitionFramesRemaining_ = 0;
  if (source_) {
    source_->Stop(channel_);
    source_ = nullptr;
  }
  lfoBank_.ClearInstrument();
  params_ = {};
  stageLfoMasks_ = {};
  clearInsertEffects();
  releasePending_ = false;
  triggerReleaseSlicesRemaining_ = 0;
  resetCommandState();
  pitchMultiplierApplied_ = false;
  currentNote_ = -1;
  previousNote_ = -1;
  legatoNotePending_ = false;
  ampEnvelope_.Kill();
  filterEnvelope_.Kill();
  resetStageState();
}

void AudioVoice::Reset() {
  lastSampleLeft_ = 0.0f;
  lastSampleRight_ = 0.0f;
  transitionFramesRemaining_ = 0;
  source_ = nullptr;
  lfoBank_.Reset();
  lfoBank_.ClearInstrument();
  params_ = {};
  stageLfoMasks_ = {};
  clearInsertEffects();
  releasePending_ = false;
  triggerReleaseSlicesRemaining_ = 0;
  resetCommandState();
  pitchMultiplierApplied_ = false;
  currentNote_ = -1;
  previousNote_ = -1;
  legatoNotePending_ = false;
  ampEnvelope_.Reset();
  filterEnvelope_.Reset();
  resetStageState();
}

bool AudioVoice::renderSourceWithControl(AudioSample *buffer,
                                         AudioBufferLayout layout,
                                         int samplecount, bool updateTick,
                                         float stepSampleCount) {
  if (samplecount <= 0) {
    applyPitchMultiplier();
    return source_->Render(channel_, buffer, samplecount, updateTick);
  }

  const int stride = GetAudioBufferLayoutStride(layout);
  int renderedSamples = 0;
  uint8_t subdivision = 1;
  bool firstChunk = true;
  // Keep the full-buffer source fast path unless a source-level control is
  // moving. Each outer buffer is one sequencer slice; proportional boundaries
  // distribute any fractional samples without accumulating timing drift.
  // This is also the insertion point for future source LFO targets.
  while (renderedSamples < samplecount) {
    applyPitchMultiplier();
    int chunkSize = samplecount - renderedSamples;
    if (hasActivePitchRamp()) {
      while (subdivision <= SourceCommandSubdivisionsPerSlice &&
             samplecount * subdivision / SourceCommandSubdivisionsPerSlice <=
                 renderedSamples) {
        ++subdivision;
      }
      if (subdivision <= SourceCommandSubdivisionsPerSlice) {
        const int subdivisionBoundary =
            samplecount * subdivision / SourceCommandSubdivisionsPerSlice;
        chunkSize = std::min(chunkSize, subdivisionBoundary - renderedSamples);
      }

      const int completionSamples =
          getPitchRampCompletionSamples(stepSampleCount);
      if (completionSamples > 0) {
        chunkSize = std::min(chunkSize, completionSamples);
      }
    }

    if (!source_->Render(channel_, buffer + renderedSamples * stride, chunkSize,
                         updateTick && firstChunk)) {
      if (renderedSamples == 0) {
        return false;
      }
      // A later chunk finishing does not invalidate the source audio already
      // rendered for this outer slice. Feed silence through the remaining
      // voice stages, then let the next outer render perform normal teardown.
      std::fill(buffer + renderedSamples * stride,
                buffer + samplecount * stride, kAudioSampleZero);
      break;
    }
    renderedSamples += chunkSize;
    firstChunk = false;
    if (hasActivePitchRamp() && stepSampleCount > 0.0f) {
      advancePitchRamps(samplesToSequencerSteps(chunkSize, stepSampleCount));
    }
  }
  return true;
}

PICOTRACKER_FAST_CODE("audio_voice_render")
bool AudioVoice::Render(AudioSample *buffer, int samplecount, bool updateTick) {
  if (!source_) {
    return false;
  }

  // Commands for a row are dispatched before its first render. If a tied-note
  // row did not consume the pending transition with LEG, discard any previous
  // LEG contribution so the new note takes effect immediately.
  if (legatoNotePending_) {
    legatoNotePending_ = false;
    FixedDurationLogPitchRampState &ramp = pitchCommand_.legato_;
    if (ramp.registered_) {
      ramp.Reset();
      markPitchMultiplierDirty();
    }
  }

  const uint32_t profileChannel = static_cast<uint32_t>(channel_);

  // Legacy sample instruments are a deliberate exception: render only the
  // instrument source here and skip the AudioVoice profiler/processing chain.
  if (source_->GetType() == IT_SAMPLE) {
    return source_->Render(channel_, buffer, samplecount, updateTick);
  }

  if (updateTick) {
    advancePitchTick();
  }

  SyncMaster *syncMaster = SyncMaster::GetInstance();
  const float stepSampleCount =
      syncMaster ? syncMaster->GetStepSampleCount() : 0.0f;

  const AudioBufferLayout sourceLayout =
      source_->GetAudioBufferLayout(channel_);
  // Reuse the upper half of the stereo render buffer as packed mono scratch.
  // Pan widens forward from that region back into the front half, so each mono
  // source sample is consumed before its destination writes can overlap it.
  AudioSample *sourceBuffer = sourceLayout == AudioBufferLayout::MonoPacked
                                  ? buffer + samplecount
                                  : buffer;

  {
    ScopedDWTProfile sourceProfile(
        GetVoiceProfileStats(VoiceProfileComponent::Source, profileChannel));
    if (!renderSourceWithControl(sourceBuffer, sourceLayout, samplecount,
                                 updateTick, stepSampleCount)) {
      // Today, "source finished" is treated as "voice finished", so the whole
      // voice is torn down immediately. If we later want tail-capable insert
      // effects (delay/reverb/etc.) to ring out after the source stops, this
      // needs to change to keep the voice chain alive until those stages go
      // idle too.
      source_ = nullptr;
      params_ = {};
      stageLfoMasks_ = {};
      clearInsertEffects();
      releasePending_ = false;
      triggerReleaseSlicesRemaining_ = 0;
      ampEnvelope_.Kill();
      filterEnvelope_.Kill();
      lfoBank_.ClearInstrument();
      resetStageState();
      return false;
    }
  }

  {
    AudioSample *prePanBuffer = sourceBuffer;
    const AudioBufferLayout prePanLayout = sourceLayout;
    AccumulatingDWTProfile bandpassProfile(
        GetVoiceProfileStats(VoiceProfileComponent::Bandwidth, profileChannel));
    AccumulatingDWTProfile standardFilterProfile(
        GetVoiceProfileStats(VoiceProfileComponent::Filter, profileChannel));
    AccumulatingDWTProfile ampProfile(
        GetVoiceProfileStats(VoiceProfileComponent::Amp, profileChannel));
    AccumulatingDWTProfile panProfile(
        GetVoiceProfileStats(VoiceProfileComponent::Pan, profileChannel));
    AccumulatingDWTProfile lfoProfile(
        GetVoiceProfileStats(VoiceProfileComponent::Lfo, profileChannel));

    syncInsertEffects();
    syncFilterType();

    runStage(lfoProfile, nullptr, stageLfoMasks_.preInsert_, prePanBuffer,
             prePanLayout, prePanBuffer, prePanLayout, samplecount,
             &AudioVoice::processPreInsertStage);
    runStage(lfoProfile, &bandpassProfile, stageLfoMasks_.bandpass_,
             prePanBuffer, prePanLayout, prePanBuffer, prePanLayout,
             samplecount, &AudioVoice::processBandpass);
    runStage(lfoProfile, &standardFilterProfile, stageLfoMasks_.standardFilter_,
             prePanBuffer, prePanLayout, prePanBuffer, prePanLayout,
             samplecount, &AudioVoice::processStandardFilter,
             &AudioVoice::updateFilterEnvelopeParameters);
    runStage(lfoProfile, &ampProfile, stageLfoMasks_.amp_, prePanBuffer,
             prePanLayout, prePanBuffer, prePanLayout, samplecount,
             &AudioVoice::processAmp, &AudioVoice::updateAmpEnvelopeParameters);
    runStage(lfoProfile, nullptr, stageLfoMasks_.postInsert_, prePanBuffer,
             prePanLayout, prePanBuffer, prePanLayout, samplecount,
             &AudioVoice::processPostInsertStage);
    runStage(lfoProfile, &panProfile, stageLfoMasks_.pan_, prePanBuffer,
             prePanLayout, buffer, AudioBufferLayout::StereoInterleaved,
             samplecount, &AudioVoice::processPan);

    // Voice command ramps now advance once per render slice so only LFO
    // targets can force stage chunking.
    advanceCommandRamps(samplesToSequencerSteps(samplecount, stepSampleCount));
  }

  const uint32_t transitionFrames =
      std::min(static_cast<uint32_t>(samplecount), transitionFramesRemaining_);
  for (uint32_t frame = 0; frame < transitionFrames; ++frame) {
    const float gain =
        static_cast<float>(transitionFramesRemaining_) / sampleTransitionFrames;
    buffer[frame * 2] =
        transitionLeft_ * gain + buffer[frame * 2] * (1.0f - gain);
    buffer[frame * 2 + 1] =
        transitionRight_ * gain + buffer[frame * 2 + 1] * (1.0f - gain);
    --transitionFramesRemaining_;
  }
  lastSampleLeft_ = buffer[(samplecount - 1) * 2];
  lastSampleRight_ = buffer[(samplecount - 1) * 2 + 1];

  advanceTriggerRelease(updateTick);

  if (releasePending_ && ampEnvelope_.IsIdle()) {
    if (source_) {
      source_->Stop(channel_);
      source_ = nullptr;
    }
    releasePending_ = false;
    triggerReleaseSlicesRemaining_ = 0;
    resetStageState();
  }
  return true;
}

void AudioVoice::refreshParameterRefs() {
  params_ = {};
  if (!source_) {
    return;
  }

  params_.bandpassCutoff_ = source_->FindVariable(FourCC::VoiceBandpassCutoff);
  params_.bandpassMix_ = source_->FindVariable(FourCC::VoiceBandpassMix);
  params_.filterCutoff_ = source_->FindVariable(FourCC::VoiceFilterCutoff);
  params_.filterResonance_ =
      source_->FindVariable(FourCC::VoiceFilterResonance);
  params_.filterType_ = source_->FindVariable(FourCC::VoiceFilterType);
  params_.ampAttack_ = source_->FindVariable(FourCC::VoiceAmpAttack);
  params_.ampDecay_ = source_->FindVariable(FourCC::VoiceAmpDecay);
  params_.ampSustain_ = source_->FindVariable(FourCC::VoiceAmpSustain);
  params_.ampRelease_ = source_->FindVariable(FourCC::VoiceAmpRelease);
  params_.triggerLength_ = source_->FindVariable(FourCC::VoiceTriggerLength);
  params_.filterAttack_ = source_->FindVariable(FourCC::VoiceFilterAttack);
  params_.filterDecay_ = source_->FindVariable(FourCC::VoiceFilterDecay);
  params_.filterSustain_ = source_->FindVariable(FourCC::VoiceFilterSustain);
  params_.filterRelease_ = source_->FindVariable(FourCC::VoiceFilterRelease);
  params_.filterEnvAmount_ =
      source_->FindVariable(FourCC::VoiceFilterEnvAmount);
  params_.amp_ = source_->FindVariable(FourCC::VoiceAmp);
  params_.pan_ = source_->FindVariable(FourCC::VoicePan);
  params_.insertType1_ = source_->FindVariable(GetVoiceInsertTypeFourCC(0));
  params_.insertAmount1_ = source_->FindVariable(GetVoiceInsertAmountFourCC(0));
  params_.insertPosition1_ =
      source_->FindVariable(GetVoiceInsertPositionFourCC(0));
  params_.insertType2_ = source_->FindVariable(GetVoiceInsertTypeFourCC(1));
  params_.insertAmount2_ = source_->FindVariable(GetVoiceInsertAmountFourCC(1));
  params_.insertPosition2_ =
      source_->FindVariable(GetVoiceInsertPositionFourCC(1));
  syncInsertEffects();
  syncFilterType();
  refreshStageLFOMasks();
}

void AudioVoice::resetTriggerRelease() {
  if (params_.triggerLength_) {
    triggerReleaseSlicesRemaining_ = static_cast<uint16_t>(std::clamp(
        params_.triggerLength_->GetInt(), 0, kMaxTriggerLengthSlices));
    if (triggerReleaseSlicesRemaining_ > 0) {
      triggerReleaseSlicesRemaining_++;
    }
  } else {
    triggerReleaseSlicesRemaining_ = 0;
  }
}

PICOTRACKER_FAST_CODE("audio_voice_advance_trigger_release")
void AudioVoice::advanceTriggerRelease(bool updateTick) {
  uint16_t &remaining = gateCommandActive_ ? gateCommandSlicesRemaining_
                                           : triggerReleaseSlicesRemaining_;
  if (!updateTick || releasePending_ || remaining == 0) {
    return;
  }

  remaining--;
  if (remaining == 0) {
    ReleaseInstrument();
  }
}

void AudioVoice::resetCommandState() {
  gateCommandSlicesRemaining_ = 0;
  gateCommandActive_ = false;
  resetCommandValue(ampCommand_);
  resetCommandValue(panCommand_);
  resetCommandValue(filterCutoffCommand_);
  resetCommandValue(filterResonanceCommand_);
  resetPitchCommands();
}

void AudioVoice::resetCommandValue(CommandValueState &state) { state = {}; }

void AudioVoice::setCommandImmediate(CommandValueState &state, float value) {
  state.current_ = value;
  state.target_ = value;
  state.stepsRemaining_ = 0.0f;
  state.overrideActive_ = true;
  state.rampActive_ = false;
}

float AudioVoice::getCommandStartValue(const CommandValueState &state,
                                       Variable *variable,
                                       int defaultValue) const {
  if (state.overrideActive_) {
    return state.current_;
  }
  return static_cast<float>(variable ? variable->GetInt() : defaultValue);
}

void AudioVoice::setCommandRamp(CommandValueState &state, float target,
                                float speedParam, Variable *variable,
                                int defaultValue, bool clampPanValue) {
  if (clampPanValue && target >= 0xFF) {
    target = 0xFE;
  }

  const float start = getCommandStartValue(state, variable, defaultValue);
  const float durationSteps = commandSpeedDurationSteps(speedParam);

  state.current_ = start;
  state.target_ = target;
  state.stepsRemaining_ = durationSteps;
  state.overrideActive_ = true;
  state.rampActive_ = durationSteps > 0.0f && start != target;

  if (!state.rampActive_) {
    state.current_ = target;
    state.stepsRemaining_ = 0.0f;
  }
}

void AudioVoice::cancelCommandRamp(CommandValueState &state) {
  state.rampActive_ = false;
  state.stepsRemaining_ = 0.0f;
}

PICOTRACKER_FAST_CODE("audio_voice_advance_command_ramp")
void AudioVoice::advanceCommandRamp(CommandValueState &state,
                                    float sequencerSteps) {
  if (!state.overrideActive_ || !state.rampActive_) {
    return;
  }

  advanceFixedDurationLinearRamp(sequencerSteps, state.target_, state.current_,
                                 state.stepsRemaining_, state.rampActive_);
}

PICOTRACKER_FAST_CODE("audio_voice_advance_command_ramps")
void AudioVoice::advanceCommandRamps(float sequencerSteps) {
  advanceCommandRamp(filterCutoffCommand_, sequencerSteps);
  advanceCommandRamp(filterResonanceCommand_, sequencerSteps);
  advanceCommandRamp(ampCommand_, sequencerSteps);
  advanceCommandRamp(panCommand_, sequencerSteps);
}

float AudioVoice::commandSpeedDurationSteps(float speedParam) {
  return speedParam / static_cast<float>(SyncMaster::SlicesPerStep);
}

void AudioVoice::refreshStageLFOMasks() {
  stageLfoMasks_ = {};
  stageLfoMasks_.bandpass_ =
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceBandpassCutoff) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceBandpassMix);
  stageLfoMasks_.standardFilter_ =
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceFilterCutoff) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceFilterResonance) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceFilterEnvAmount) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceFilterAttack) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceFilterDecay) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceFilterSustain) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceFilterRelease);
  stageLfoMasks_.amp_ =
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceAmp) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceAmpAttack) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceAmpDecay) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceAmpSustain) |
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceAmpRelease);
  stageLfoMasks_.pan_ = lfoBank_.GetSlotMaskForTarget(LFOTarget::VoicePan);

  const uint8_t insert1Mask =
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceInsertAmount1);
  const uint8_t insert2Mask =
      lfoBank_.GetSlotMaskForTarget(LFOTarget::VoiceInsertAmount2);
  if (insertPositions_[0] == kVoiceInsertPositionPre) {
    stageLfoMasks_.preInsert_ |= insert1Mask;
  } else {
    stageLfoMasks_.postInsert_ |= insert1Mask;
  }
  if (insertPositions_[1] == kVoiceInsertPositionPre) {
    stageLfoMasks_.preInsert_ |= insert2Mask;
  } else {
    stageLfoMasks_.postInsert_ |= insert2Mask;
  }
}

void AudioVoice::resetStageState() {
  for (auto &state : bandpassState_) {
    state.highpassLow_ = 0.0f;
    state.lowpassOut_ = 0.0f;
  }
  for (auto &state : standardFilterState_) {
    state.ic1eq_ = 0.0f;
    state.ic2eq_ = 0.0f;
  }
  bandpassCoefficients_ = {};
  for (int slot = 0; slot < InsertSlotCount; ++slot) {
    insertEffectSlots_[slot].ResetState();
  }
}

PICOTRACKER_FAST_CODE("audio_voice_update_filter_envelope")
void AudioVoice::updateFilterEnvelopeParameters() {
  filterEnvelope_.SetParameters(
      getVoiceValue(LFOTarget::VoiceFilterAttack, params_.filterAttack_, 0, 0,
                    0xFF),
      getVoiceValue(LFOTarget::VoiceFilterDecay, params_.filterDecay_, 0, 0,
                    0xFF),
      getVoiceValue(LFOTarget::VoiceFilterSustain, params_.filterSustain_, 0, 0,
                    0xFF),
      getVoiceValue(LFOTarget::VoiceFilterRelease, params_.filterRelease_, 0, 0,
                    0xFF));
}

PICOTRACKER_FAST_CODE("audio_voice_update_amp_envelope")
void AudioVoice::updateAmpEnvelopeParameters() {
  const int release = getVoiceValue(LFOTarget::VoiceAmpRelease,
                                    params_.ampRelease_, 0, 0, 0xFF);
  ampEnvelope_.SetParameters(
      getVoiceValue(LFOTarget::VoiceAmpAttack, params_.ampAttack_, 0, 0, 0xFF),
      getVoiceValue(LFOTarget::VoiceAmpDecay, params_.ampDecay_, 0, 0, 0xFF),
      getVoiceValue(LFOTarget::VoiceAmpSustain, params_.ampSustain_, 0xFF, 0,
                    0xFF),
      source_ && source_->GetType() == IT_MSAMPLER ? std::max(1, release)
                                                   : release);
}

PICOTRACKER_FAST_CODE("audio_voice_sync_filter_type")
void AudioVoice::syncFilterType() {
  const int filterTypeValue = std::clamp(
      params_.filterType_ ? params_.filterType_->GetInt() : 0, 0, 0xFF);
  if (filterMorph_.value_ == filterTypeValue) {
    return;
  }

  const float filterType = byteToUnit(filterTypeValue);
  const float lowToBand = std::min(filterType * 2.0f, 1.0f);
  const float bandToHigh = std::max(filterType * 2.0f - 1.0f, 0.0f);
  const float lowBandAmount = 1.0f - bandToHigh;

  filterMorph_.value_ = filterTypeValue;
  filterMorph_.inputWeight_ = bandToHigh;
  filterMorph_.bandpassBaseWeight_ = lowBandAmount * lowToBand;
  filterMorph_.highpassFeedbackWeight_ = bandToHigh;
  filterMorph_.lowpassWeight_ = lowBandAmount * (1.0f - lowToBand) - bandToHigh;
}

PICOTRACKER_FAST_CODE("audio_voice_run_stage")
void AudioVoice::runStage(
    AccumulatingDWTProfile &lfoProfile, AccumulatingDWTProfile *stageProfile,
    uint8_t lfoMask, AudioSample *inputBuffer, AudioBufferLayout inputLayout,
    AudioSample *outputBuffer, AudioBufferLayout outputLayout, int samplecount,
    void (AudioVoice::*processStage)(AudioSample *, AudioSample *, int,
                                     AudioBufferLayout, AudioBufferLayout),
    void (AudioVoice::*beforeStage)()) {
  if (lfoMask == 0) {
    if (beforeStage) {
      (this->*beforeStage)();
    }
    if (stageProfile) {
      stageProfile->Begin();
    }
    (this->*processStage)(inputBuffer, outputBuffer, samplecount, inputLayout,
                          outputLayout);
    if (stageProfile) {
      stageProfile->End();
    }
    return;
  }

  int processedSamples = 0;
  const int inputStride = GetAudioBufferLayoutChannelCount(inputLayout);
  const int outputStride = GetAudioBufferLayoutChannelCount(outputLayout);
  while (processedSamples < samplecount) {
    const int chunkSize =
        std::min(kLFOControlQuantumSamples, samplecount - processedSamples);
    AudioSample *chunkInput = inputBuffer + processedSamples * inputStride;
    AudioSample *chunkOutput = outputBuffer + processedSamples * outputStride;
    if (beforeStage) {
      (this->*beforeStage)();
    }
    if (stageProfile) {
      stageProfile->Begin();
    }
    (this->*processStage)(chunkInput, chunkOutput, chunkSize, inputLayout,
                          outputLayout);
    if (stageProfile) {
      stageProfile->End();
    }

    if (lfoMask != 0) {
      lfoProfile.Begin();
      lfoBank_.AdvanceSlots(lfoMask, chunkSize);
      lfoProfile.End();
    }

    processedSamples += chunkSize;
  }
}

PICOTRACKER_FAST_CODE("audio_voice_get_value")
int AudioVoice::getVoiceValue(LFOTarget target, Variable *variable,
                              int defaultValue, int minValue,
                              int maxValue) const {
  const CommandValueState *commandState = nullptr;
  switch (target) {
  case LFOTarget::VoiceFilterCutoff:
    commandState = &filterCutoffCommand_;
    break;
  case LFOTarget::VoiceFilterResonance:
    commandState = &filterResonanceCommand_;
    break;
  case LFOTarget::VoiceAmp:
    commandState = &ampCommand_;
    break;
  case LFOTarget::VoicePan:
    commandState = &panCommand_;
    break;
  default:
    break;
  }

  int baseValue = variable ? variable->GetInt() : defaultValue;
  if (commandState && commandState->overrideActive_) {
    baseValue = static_cast<int>(std::lround(commandState->current_));
  }
  return lfoBank_.GetLiveModulatedValue(target, baseValue, minValue, maxValue);
}

PICOTRACKER_FAST_CODE("audio_voice_pre_insert")
void AudioVoice::processPreInsertStage(AudioSample *input, AudioSample *output,
                                       int samplecount,
                                       AudioBufferLayout inputLayout,
                                       AudioBufferLayout outputLayout) {
  (void)input;
  (void)outputLayout;
  processInsertChain(preInsertChain_, output, samplecount, inputLayout);
}

PICOTRACKER_FAST_CODE("audio_voice_bandpass")
void AudioVoice::processBandpass(AudioSample *input, AudioSample *output,
                                 int samplecount, AudioBufferLayout inputLayout,
                                 AudioBufferLayout outputLayout) {
  (void)outputLayout;
  const int base = getVoiceValue(LFOTarget::VoiceBandpassCutoff,
                                 params_.bandpassCutoff_, 0, 0, 0xFF);
  const int width = getVoiceValue(LFOTarget::VoiceBandpassMix,
                                  params_.bandpassMix_, 0xFF, 0, 0xFF);
  const bool highpassActive = base > 0;
  const bool lowpassActive = width < 0xFF;
  // Both coefficients depend only on the quantized controls, so unchanged
  // controls can reuse the exact previous results of powf/expf.
  if (bandpassCoefficients_.base_ != base ||
      bandpassCoefficients_.width_ != width) {
    const float maxHz = maxCutoffHz();
    const float baseHz = highpassActive ? mapCutoffHz(base) : 0.0f;
    float lowpassHz = maxHz;

    if (lowpassActive) {
      if (!highpassActive) {
        lowpassHz = mapCutoffHz(width);
      } else {
        lowpassHz = baseHz + byteToUnit(width) * (maxHz - baseHz);
      }
    }

    bandpassCoefficients_.base_ = base;
    bandpassCoefficients_.width_ = width;
    bandpassCoefficients_.highpassAlpha_ =
        highpassActive ? onePoleAlpha(baseHz) : 0.0f;
    bandpassCoefficients_.lowpassAlpha_ =
        lowpassActive ? onePoleAlpha(lowpassHz) : 0.0f;
  }

  const float highpassAlpha = bandpassCoefficients_.highpassAlpha_;
  const float lowpassAlpha = bandpassCoefficients_.lowpassAlpha_;

  if (inputLayout == AudioBufferLayout::MonoPacked) {
    float highpassLow = bandpassState_[0].highpassLow_;
    float lowpassOut = bandpassState_[0].lowpassOut_;
    for (int i = 0; i < samplecount; ++i) {
      const float inputSample = input[i];
      float stageOutput = inputSample;

      if (highpassActive) {
        highpassLow += highpassAlpha * (inputSample - highpassLow);
        stageOutput = inputSample - highpassLow;
      } else {
        highpassLow = inputSample;
      }

      if (lowpassActive) {
        lowpassOut += lowpassAlpha * (stageOutput - lowpassOut);
        stageOutput = lowpassOut;
      } else {
        lowpassOut = stageOutput;
      }

      output[i] = stageOutput;
    }
    bandpassState_[0].highpassLow_ = highpassLow;
    bandpassState_[0].lowpassOut_ = lowpassOut;
    return;
  }

  float highpassLowLeft = bandpassState_[0].highpassLow_;
  float lowpassOutLeft = bandpassState_[0].lowpassOut_;
  float highpassLowRight = bandpassState_[1].highpassLow_;
  float lowpassOutRight = bandpassState_[1].lowpassOut_;
  for (int i = 0; i < samplecount; ++i) {
    const int index = i * 2;
    const float inputLeft = input[index];
    const float inputRight = input[index + 1];
    float outputLeft = inputLeft;
    float outputRight = inputRight;

    if (highpassActive) {
      highpassLowLeft += highpassAlpha * (inputLeft - highpassLowLeft);
      highpassLowRight += highpassAlpha * (inputRight - highpassLowRight);
      outputLeft = inputLeft - highpassLowLeft;
      outputRight = inputRight - highpassLowRight;
    } else {
      highpassLowLeft = inputLeft;
      highpassLowRight = inputRight;
    }

    if (lowpassActive) {
      lowpassOutLeft += lowpassAlpha * (outputLeft - lowpassOutLeft);
      lowpassOutRight += lowpassAlpha * (outputRight - lowpassOutRight);
      outputLeft = lowpassOutLeft;
      outputRight = lowpassOutRight;
    } else {
      lowpassOutLeft = outputLeft;
      lowpassOutRight = outputRight;
    }

    output[index] = outputLeft;
    output[index + 1] = outputRight;
  }
  bandpassState_[0].highpassLow_ = highpassLowLeft;
  bandpassState_[0].lowpassOut_ = lowpassOutLeft;
  bandpassState_[1].highpassLow_ = highpassLowRight;
  bandpassState_[1].lowpassOut_ = lowpassOutRight;
}

PICOTRACKER_FAST_CODE("audio_voice_standard_filter")
void AudioVoice::processStandardFilter(AudioSample *input, AudioSample *output,
                                       int samplecount,
                                       AudioBufferLayout inputLayout,
                                       AudioBufferLayout outputLayout) {
  (void)outputLayout;
  // Note: Filter defaults are musically "open", but not technically a fully
  // transparent bypass because the lowpass filter remains in the signal path.
  const float q = mapResonanceQ(getVoiceValue(
      LFOTarget::VoiceFilterResonance, params_.filterResonance_, 0, 0, 0xFF));
  const float k = 1.0f / q;
  const float baseCutoff = byteToUnit(getVoiceValue(
      LFOTarget::VoiceFilterCutoff, params_.filterCutoff_, 0xFF, 0, 0xFF));
  const float inputWeight = filterMorph_.inputWeight_;
  const float bandpassWeight = filterMorph_.bandpassBaseWeight_ -
                               filterMorph_.highpassFeedbackWeight_ * k;
  const float lowpassWeight = filterMorph_.lowpassWeight_;
  const float envAmount = bipolarDepth(getVoiceValue(
      LFOTarget::VoiceFilterEnvAmount, params_.filterEnvAmount_, 0, -64, 64));
  const bool constantCutoff = envAmount == 0.0f;
  StandardFilterCoefficients constantCoefficients{};
  // A zero envelope depth keeps the cutoff constant even while the envelope
  // advances, allowing the exact coefficient calculation to move out of the
  // sample loops.
  if (constantCutoff) {
    constantCoefficients = calculateStandardFilterCoefficients(baseCutoff, k);
  }

  if (inputLayout == AudioBufferLayout::MonoPacked) {
    float ic1eq = standardFilterState_[0].ic1eq_;
    float ic2eq = standardFilterState_[0].ic2eq_;
    int processedSamples = 0;
    while (processedSamples < samplecount) {
      const VoiceADSR::LinearSegment envelopeSegment =
          filterEnvelope_.AdvanceBlock(samplecount - processedSamples);
      if (envelopeSegment.samples <= 0) {
        break;
      }

      float envValue = envelopeSegment.firstValue;
      const int segmentEnd = processedSamples + envelopeSegment.samples;
      if (constantCutoff || envelopeSegment.step == 0.0f) {
        const StandardFilterCoefficients coefficients =
            constantCutoff
                ? constantCoefficients
                : calculateStandardFilterCoefficients(
                      std::clamp(baseCutoff + envAmount * envValue, 0.0f, 1.0f),
                      k);
        for (; processedSamples < segmentEnd; ++processedSamples) {
          output[processedSamples] = processStandardFilterSample(
              input[processedSamples], coefficients, inputWeight,
              bandpassWeight, lowpassWeight, ic1eq, ic2eq);
          envValue += envelopeSegment.step;
        }
      } else {
        for (; processedSamples < segmentEnd; ++processedSamples) {
          const float cutoff =
              std::clamp(baseCutoff + envAmount * envValue, 0.0f, 1.0f);
          const StandardFilterCoefficients coefficients =
              calculateStandardFilterCoefficients(cutoff, k);
          output[processedSamples] = processStandardFilterSample(
              input[processedSamples], coefficients, inputWeight,
              bandpassWeight, lowpassWeight, ic1eq, ic2eq);
          envValue += envelopeSegment.step;
        }
      }
    }
    standardFilterState_[0].ic1eq_ = ic1eq;
    standardFilterState_[0].ic2eq_ = ic2eq;
    return;
  }

  float ic1eqLeft = standardFilterState_[0].ic1eq_;
  float ic2eqLeft = standardFilterState_[0].ic2eq_;
  float ic1eqRight = standardFilterState_[1].ic1eq_;
  float ic2eqRight = standardFilterState_[1].ic2eq_;
  int processedSamples = 0;
  while (processedSamples < samplecount) {
    const VoiceADSR::LinearSegment envelopeSegment =
        filterEnvelope_.AdvanceBlock(samplecount - processedSamples);
    if (envelopeSegment.samples <= 0) {
      break;
    }

    float envValue = envelopeSegment.firstValue;
    const int segmentEnd = processedSamples + envelopeSegment.samples;
    if (constantCutoff || envelopeSegment.step == 0.0f) {
      const StandardFilterCoefficients coefficients =
          constantCutoff
              ? constantCoefficients
              : calculateStandardFilterCoefficients(
                    std::clamp(baseCutoff + envAmount * envValue, 0.0f, 1.0f),
                    k);
      for (; processedSamples < segmentEnd; ++processedSamples) {
        const int index = processedSamples * 2;
        const float inputLeft = input[index];
        const float inputRight = input[index + 1];
        const float outputLeft = processStandardFilterSample(
            inputLeft, coefficients, inputWeight, bandpassWeight, lowpassWeight,
            ic1eqLeft, ic2eqLeft);
        const float outputRight = processStandardFilterSample(
            inputRight, coefficients, inputWeight, bandpassWeight,
            lowpassWeight, ic1eqRight, ic2eqRight);
        output[index] = outputLeft;
        output[index + 1] = outputRight;
        envValue += envelopeSegment.step;
      }
    } else {
      for (; processedSamples < segmentEnd; ++processedSamples) {
        const float cutoff =
            std::clamp(baseCutoff + envAmount * envValue, 0.0f, 1.0f);
        const StandardFilterCoefficients coefficients =
            calculateStandardFilterCoefficients(cutoff, k);
        const int index = processedSamples * 2;
        const float inputLeft = input[index];
        const float inputRight = input[index + 1];
        const float outputLeft = processStandardFilterSample(
            inputLeft, coefficients, inputWeight, bandpassWeight, lowpassWeight,
            ic1eqLeft, ic2eqLeft);
        const float outputRight = processStandardFilterSample(
            inputRight, coefficients, inputWeight, bandpassWeight,
            lowpassWeight, ic1eqRight, ic2eqRight);
        output[index] = outputLeft;
        output[index + 1] = outputRight;
        envValue += envelopeSegment.step;
      }
    }
  }
  standardFilterState_[0].ic1eq_ = ic1eqLeft;
  standardFilterState_[0].ic2eq_ = ic2eqLeft;
  standardFilterState_[1].ic1eq_ = ic1eqRight;
  standardFilterState_[1].ic2eq_ = ic2eqRight;
}

PICOTRACKER_FAST_CODE("audio_voice_amp")
void AudioVoice::processAmp(AudioSample *input, AudioSample *output,
                            int samplecount, AudioBufferLayout inputLayout,
                            AudioBufferLayout outputLayout) {
  (void)outputLayout;
  const int amp =
      getVoiceValue(LFOTarget::VoiceAmp, params_.amp_, 0xFF, 0, 0xFF);
  const float baseGain = byteToUnit(amp);
  const int channelCount = GetAudioBufferLayoutChannelCount(inputLayout);

  int processedSamples = 0;
  while (processedSamples < samplecount) {
    const VoiceADSR::LinearSegment envelopeSegment =
        ampEnvelope_.AdvanceBlock(samplecount - processedSamples);
    if (envelopeSegment.samples <= 0) {
      break;
    }

    if (releasePending_ && ampEnvelope_.IsIdle() &&
        envelopeSegment.firstValue <= kEnvelopeEpsilon &&
        envelopeSegment.step == 0.0f) {
      for (int remaining = processedSamples * channelCount;
           remaining < samplecount * channelCount; ++remaining) {
        output[remaining] = 0;
      }
      break;
    }

    AudioSample gain = baseGain * envelopeSegment.firstValue;
    const AudioSample gainStep = baseGain * envelopeSegment.step;
    const int segmentEnd = processedSamples + envelopeSegment.samples;
    for (; processedSamples < segmentEnd; ++processedSamples) {
      const int index = processedSamples * channelCount;
      output[index] = input[index] * gain;
      if (channelCount > 1) {
        output[index + 1] = input[index + 1] * gain;
      }
      gain += gainStep;
    }
  }
}

PICOTRACKER_FAST_CODE("audio_voice_pan")
void AudioVoice::processPan(AudioSample *input, AudioSample *output,
                            int samplecount, AudioBufferLayout inputLayout,
                            AudioBufferLayout outputLayout) {
  (void)outputLayout;
  const int pan =
      clampPan(getVoiceValue(LFOTarget::VoicePan, params_.pan_, 0x7F, 0, 0xFF));

  const AudioSample left = panGainLeft(pan);
  const AudioSample right = panGainRight(pan);
  if (inputLayout == AudioBufferLayout::MonoPacked) {
    for (int i = 0; i < samplecount; ++i) {
      const AudioSample mono = input[i];
      const int index = i * 2;
      output[index] = mono * right;
      output[index + 1] = mono * left;
    }
    return;
  }

  for (int i = 0; i < samplecount; ++i) {
    const int index = i * 2;
    const AudioSample inputLeft = input[index];
    const AudioSample inputRight = input[index + 1];

    // Swap the logical stereo pair here to compensate for the reversed
    // hardware output wiring. This keeps the legacy sample path untouched
    // while every voiced instrument that flows through AudioVoice is fixed in
    // one place.
    output[index] = inputRight * right;
    output[index + 1] = inputLeft * left;
  }
}

PICOTRACKER_FAST_CODE("audio_voice_post_insert")
void AudioVoice::processPostInsertStage(AudioSample *input, AudioSample *output,
                                        int samplecount,
                                        AudioBufferLayout inputLayout,
                                        AudioBufferLayout outputLayout) {
  (void)input;
  (void)outputLayout;
  processInsertChain(postInsertChain_, output, samplecount, inputLayout);
}

PICOTRACKER_FAST_CODE("audio_voice_insert_chain")
void AudioVoice::processInsertChain(etl::ivector<InsertEffectSlot *> &chain,
                                    AudioSample *buffer, int samplecount,
                                    AudioBufferLayout layout) {
  for (auto *slot : chain) {
    const int index = static_cast<int>(slot - &insertEffectSlots_[0]);
    Variable *amount =
        index == 0 ? params_.insertAmount1_ : params_.insertAmount2_;
    const LFOTarget target = index == 0 ? LFOTarget::VoiceInsertAmount1
                                        : LFOTarget::VoiceInsertAmount2;
    const int slotAmount =
        getVoiceValue(target, amount, kDefaultVoiceInsertAmount, 0, 0xFF);
    const VoiceProfileComponent component =
        index == 0 ? VoiceProfileComponent::Insert1
                   : VoiceProfileComponent::Insert2;
    ScopedDWTProfile effectProfile(
        GetVoiceProfileStats(component, static_cast<uint32_t>(channel_)));
    slot->Render(buffer, samplecount, slotAmount, layout);
  }
}

InsertEffectProcessor *AudioVoice::createInsertEffect(int insertType) {
  switch (ClampVoiceInsertEffectIndex(insertType)) {
  case kVoiceInsertEffectPassthrough:
    return insertEffectPool_.create<PassthroughInsertEffect>();
  case kVoiceInsertEffectOverdrive:
    return insertEffectPool_.create<OverdriveInsertEffect>();
  case kVoiceInsertEffectBitcrush:
    return insertEffectPool_.create<BitcrushInsertEffect>();
  case kVoiceInsertEffectDownsample:
    return insertEffectPool_.create<DownsampleInsertEffect>();
  case kVoiceInsertEffectSlew:
    return insertEffectPool_.create<SlewInsertEffect>();
  case kVoiceInsertEffectRectify:
    return insertEffectPool_.create<RectifyInsertEffect>();
  default:
    return nullptr;
  }
}

void AudioVoice::destroyInsertEffectSlot(int slot) {
  InsertEffectProcessor *effect = insertEffectSlots_[slot].GetEffect();
  if (!effect) {
    insertTypes_[slot] = kDefaultVoiceInsertEffectIndex;
    return;
  }

  switch (ClampVoiceInsertEffectIndex(insertTypes_[slot])) {
  case kVoiceInsertEffectPassthrough:
    insertEffectPool_.destroy(static_cast<PassthroughInsertEffect *>(effect));
    break;
  case kVoiceInsertEffectOverdrive:
    insertEffectPool_.destroy(static_cast<OverdriveInsertEffect *>(effect));
    break;
  case kVoiceInsertEffectBitcrush:
    insertEffectPool_.destroy(static_cast<BitcrushInsertEffect *>(effect));
    break;
  case kVoiceInsertEffectDownsample:
    insertEffectPool_.destroy(static_cast<DownsampleInsertEffect *>(effect));
    break;
  case kVoiceInsertEffectSlew:
    insertEffectPool_.destroy(static_cast<SlewInsertEffect *>(effect));
    break;
  case kVoiceInsertEffectRectify:
    insertEffectPool_.destroy(static_cast<RectifyInsertEffect *>(effect));
    break;
  default:
    break;
  }

  effect = nullptr;
  insertTypes_[slot] = kDefaultVoiceInsertEffectIndex;
}

void AudioVoice::clearInsertEffects() {
  preInsertChain_.clear();
  postInsertChain_.clear();
  for (int slot = 0; slot < InsertSlotCount; ++slot) {
    destroyInsertEffectSlot(slot);
    insertEffectSlots_[slot].SetEffect(
        createInsertEffect(kDefaultVoiceInsertEffectIndex));
    insertTypes_[slot] = kDefaultVoiceInsertEffectIndex;
    insertPositions_[slot] = kDefaultVoiceInsertPosition;
  }
  rebuildInsertChains();
}

PICOTRACKER_FAST_CODE("audio_voice_sync_insert_effects")
void AudioVoice::syncInsertEffects() {
  const bool slot0Changed =
      syncInsertEffectSlot(0, params_.insertType1_, params_.insertPosition1_);
  const bool slot1Changed =
      syncInsertEffectSlot(1, params_.insertType2_, params_.insertPosition2_);
  if (slot0Changed || slot1Changed) {
    rebuildInsertChains();
    refreshStageLFOMasks();
  }
}

PICOTRACKER_FAST_CODE("audio_voice_sync_insert_effect_slot")
bool AudioVoice::syncInsertEffectSlot(int slot, Variable *type,
                                      Variable *position) {
  const int slotType = ClampVoiceInsertEffectIndex(
      type ? type->GetInt() : kDefaultVoiceInsertEffectIndex);
  const int slotPosition = ClampVoiceInsertPositionIndex(
      position ? position->GetInt() : kDefaultVoiceInsertPosition);
  bool changed = false;

  if (insertTypes_[slot] != slotType) {
    destroyInsertEffectSlot(slot);
    InsertEffectProcessor *effect = createInsertEffect(slotType);
    insertEffectSlots_[slot].SetEffect(effect);
    insertTypes_[slot] = slotType;
    insertEffectSlots_[slot].ResetState();
    changed = true;
  }

  if (insertPositions_[slot] != slotPosition) {
    insertEffectSlots_[slot].ResetState();
    changed = true;
  }

  insertPositions_[slot] = slotPosition;
  return changed;
}

void AudioVoice::rebuildInsertChains() {
  preInsertChain_.clear();
  postInsertChain_.clear();

  for (int slot = 0; slot < InsertSlotCount; ++slot) {
    if (insertPositions_[slot] == kVoiceInsertPositionPre) {
      preInsertChain_.push_back(&insertEffectSlots_[slot]);
    } else {
      postInsertChain_.push_back(&insertEffectSlots_[slot]);
    }
  }
}
