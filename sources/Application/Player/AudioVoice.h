/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _AUDIO_VOICE_H_
#define _AUDIO_VOICE_H_

#include "Application/Audio/AudioBufferLayout.h"
#include "Application/Audio/AudioSample.h"
#include "Application/Instruments/I_Instrument.h"
#include "BitcrushInsertEffect.h"
#include "ClipInsertEffect.h"
#include "DCOffsetInsertEffect.h"
#include "DetuneInsertEffect.h"
#include "DownsampleInsertEffect.h"
#include "Externals/etl/include/etl/variant_pool.h"
#include "Externals/etl/include/etl/vector.h"
#include "InsertEffectDefinitions.h"
#include "InsertEffectSlot.h"
#include "LFOProcessor.h"
#include "OverdriveInsertEffect.h"
#include "PassthroughInsertEffect.h"
#include "RectifyInsertEffect.h"
#include "SlewInsertEffect.h"
#include "VoiceADSR.h"
#include <cstdint>

struct DWTProfileStats;
class AccumulatingDWTProfile;

enum class VoiceProfileComponent : uint8_t {
  Source,
  Bandwidth,
  Filter,
  Amp,
  Pan,
  Lfo,
  Insert1,
  Insert2,
  PlayerControl,
  WetDry,
  Count,
};

DWTProfileStats *GetVoiceProfileStats(VoiceProfileComponent component,
                                      uint32_t channel);
DWTProfileStats *GetVoiceChannelTotalProfileStats(uint32_t channel);
DWTProfileStats *GetVoicesTotalProfileStats();

class AudioVoice {
public:
  explicit AudioVoice(int channel);

  bool StartInstrument(I_Instrument *instrument, unsigned char note,
                       InstrumentStartMode mode, uint8_t velocity = 127);
  bool ProcessCommand(TrackerCommand command, ushort value);
  void ReleaseInstrument();
  void StopInstrument();
  void Reset();

  I_Instrument *GetInstrument() const { return source_; }

  bool Render(AudioSample *buffer, int samplecount, bool updateTick);

private:
  static constexpr int InsertSlotCount = 2;
  // Source pitch ramps are evaluated this many times per sequencer slice.
  // Their remaining duration is stored in sequencer steps, so changing this
  // only affects smoothness.
  static constexpr uint8_t SourceCommandSubdivisionsPerSlice = 8;
  static constexpr uint8_t PitchUpdaterCapacity = 8;
  using InsertEffectPool =
      etl::variant_pool<InsertSlotCount, PassthroughInsertEffect,
                        OverdriveInsertEffect, BitcrushInsertEffect,
                        DownsampleInsertEffect, SlewInsertEffect,
                        RectifyInsertEffect>;

  struct ParameterRefs {
    Variable *bandpassCutoff_ = nullptr;
    Variable *bandpassMix_ = nullptr;
    Variable *filterCutoff_ = nullptr;
    Variable *filterResonance_ = nullptr;
    Variable *filterType_ = nullptr;
    Variable *ampAttack_ = nullptr;
    Variable *ampDecay_ = nullptr;
    Variable *ampSustain_ = nullptr;
    Variable *ampRelease_ = nullptr;
    Variable *triggerLength_ = nullptr;
    Variable *filterAttack_ = nullptr;
    Variable *filterDecay_ = nullptr;
    Variable *filterSustain_ = nullptr;
    Variable *filterRelease_ = nullptr;
    Variable *filterEnvAmount_ = nullptr;
    Variable *amp_ = nullptr;
    Variable *pan_ = nullptr;
    Variable *insertType1_ = nullptr;
    Variable *insertAmount1_ = nullptr;
    Variable *insertPosition1_ = nullptr;
    Variable *insertType2_ = nullptr;
    Variable *insertAmount2_ = nullptr;
    Variable *insertPosition2_ = nullptr;
  };

  struct BaseWidthState {
    float highpassLow_ = 0.0f;
    float lowpassOut_ = 0.0f;
  };

  struct BandpassCoefficientState {
    int base_ = -1;
    int width_ = -1;
    float highpassAlpha_ = 0.0f;
    float lowpassAlpha_ = 0.0f;
  };

  struct StandardFilterState {
    float ic1eq_ = 0.0f;
    float ic2eq_ = 0.0f;
  };

  struct FilterMorphState {
    int value_ = -1;
    float inputWeight_ = 0.0f;
    float bandpassBaseWeight_ = 0.0f;
    float highpassFeedbackWeight_ = 0.0f;
    float lowpassWeight_ = 1.0f;
  };

  struct StageLFOMasks {
    uint8_t preInsert_ = 0;
    uint8_t bandpass_ = 0;
    uint8_t standardFilter_ = 0;
    uint8_t amp_ = 0;
    uint8_t pan_ = 0;
    uint8_t postInsert_ = 0;
  };

  struct CommandValueState {
    float current_ = 0.0f;
    float target_ = 0.0f;
    float stepsRemaining_ = 0.0f;
    bool overrideActive_ = false;
    bool rampActive_ = false;
  };

  struct PitchUpdater {
    virtual ~PitchUpdater() = default;
    virtual bool AdvanceControl(float sequencerSteps) {
      (void)sequencerSteps;
      return false;
    }
    virtual bool AdvanceTick() { return false; }
    virtual bool HasActiveRamp() const { return false; }
    virtual float GetCurrentRatio() const = 0;
    virtual void Reset() = 0;

    bool registered_ = false;
  };

  struct FixedDurationLinearPitchRampState final : PitchUpdater {
    bool AdvanceControl(float sequencerSteps) override;
    bool HasActiveRamp() const override { return active_; }
    float GetCurrentRatio() const override { return current_; }
    void Reset() override;

    float current_ = 1.0f;
    float target_ = 1.0f;
    float stepsRemaining_ = 0.0f;
    bool active_ = false;
  };

  struct FixedDurationLogPitchRampState final : PitchUpdater {
    bool AdvanceControl(float sequencerSteps) override;
    bool HasActiveRamp() const override { return active_; }
    float GetCurrentRatio() const override { return current_; }
    void Reset() override;

    float current_ = 1.0f;
    float target_ = 1.0f;
    float stepsRemaining_ = 0.0f;
    bool active_ = false;
  };

  struct ArpeggiatorState final : PitchUpdater {
    void Configure(uint16_t value);
    bool AdvanceTick() override;
    float GetCurrentRatio() const override { return current_; }
    void Reset() override;

    uint8_t values_[5] = {};
    uint8_t position_ = 0;
    uint8_t length_ = 0;
    float current_ = 1.0f;
    bool active_ = false;
  };

  struct PitchCommandState {
    FixedDurationLinearPitchRampState pitchSlide_;
    FixedDurationLogPitchRampState legato_;
    FixedDurationLogPitchRampState fineTune_;
    ArpeggiatorState arpeggiator_;
    float currentMultiplier_ = 1.0f;
    bool multiplierDirty_ = true;
    etl::vector<PitchUpdater *, PitchUpdaterCapacity> contributors_;
  };

  void refreshParameterRefs();
  void refreshStageLFOMasks();
  void resetTriggerRelease();
  void advanceTriggerRelease(bool updateTick);
  void resetCommandState();
  void resetCommandValue(CommandValueState &state);
  void setCommandImmediate(CommandValueState &state, float value);
  float getCommandStartValue(const CommandValueState &state, Variable *variable,
                             int defaultValue) const;
  void setCommandRamp(CommandValueState &state, float target, float speedParam,
                      Variable *variable, int defaultValue,
                      bool clampPanValue = false);
  void cancelCommandRamp(CommandValueState &state);
  void advanceCommandRamp(CommandValueState &state, float sequencerSteps);
  void advanceCommandRamps(float sequencerSteps);
  // This is the single speed-byte conversion used by every AudioVoice ramp.
  // One speed unit is one sequencer slice.
  static float commandSpeedDurationSteps(float speedParam);
  bool processPitchCommand(TrackerCommand command, ushort value);
  bool registerPitchUpdater(PitchUpdater &updater);
  void resetPitchCommands();
  void advancePitchTick();
  void advancePitchRamps(float sequencerSteps);
  int getPitchRampCompletionSamples(float stepSampleCount) const;
  bool hasActivePitchRamp() const;
  void markPitchMultiplierDirty();
  void updatePitchMultiplier();
  void applyPitchMultiplier();
  bool renderSourceWithControl(AudioSample *buffer, AudioBufferLayout layout,
                               int samplecount, bool updateTick,
                               float stepSampleCount);
  void resetStageState();
  void updateFilterEnvelopeParameters();
  void updateAmpEnvelopeParameters();
  void syncFilterType();
  void runStage(AccumulatingDWTProfile &lfoProfile,
                AccumulatingDWTProfile *stageProfile, uint8_t lfoMask,
                AudioSample *inputBuffer, AudioBufferLayout inputLayout,
                AudioSample *outputBuffer, AudioBufferLayout outputLayout,
                int samplecount,
                void (AudioVoice::*processStage)(AudioSample *, AudioSample *,
                                                 int, AudioBufferLayout,
                                                 AudioBufferLayout),
                void (AudioVoice::*beforeStage)() = nullptr);
  int getVoiceValue(LFOTarget target, Variable *variable, int defaultValue,
                    int minValue, int maxValue) const;

  void processPreInsertStage(AudioSample *input, AudioSample *output,
                             int samplecount, AudioBufferLayout inputLayout,
                             AudioBufferLayout outputLayout);
  void processBandpass(AudioSample *input, AudioSample *output, int samplecount,
                       AudioBufferLayout inputLayout,
                       AudioBufferLayout outputLayout);
  void processStandardFilter(AudioSample *input, AudioSample *output,
                             int samplecount, AudioBufferLayout inputLayout,
                             AudioBufferLayout outputLayout);
  void processAmp(AudioSample *input, AudioSample *output, int samplecount,
                  AudioBufferLayout inputLayout,
                  AudioBufferLayout outputLayout);
  void processPan(AudioSample *input, AudioSample *output, int samplecount,
                  AudioBufferLayout inputLayout,
                  AudioBufferLayout outputLayout);
  void processPostInsertStage(AudioSample *input, AudioSample *output,
                              int samplecount, AudioBufferLayout inputLayout,
                              AudioBufferLayout outputLayout);
  void processInsertChain(etl::ivector<InsertEffectSlot *> &chain,
                          AudioSample *buffer, int samplecount,
                          AudioBufferLayout layout);
  InsertEffectProcessor *createInsertEffect(int insertType);
  void destroyInsertEffectSlot(int slot);
  void clearInsertEffects();
  void syncInsertEffects();
  bool syncInsertEffectSlot(int slot, Variable *type, Variable *position);
  void rebuildInsertChains();

  // Smooth MSAMPLER note replacement after the shared envelope and FX, where
  // continuity is measured in the samples actually sent to the mixer.
  static constexpr uint32_t sampleTransitionFrames = 64;
  AudioSample lastSampleLeft_ = 0.0f;
  AudioSample lastSampleRight_ = 0.0f;
  AudioSample transitionLeft_ = 0.0f;
  AudioSample transitionRight_ = 0.0f;
  uint32_t transitionFramesRemaining_ = 0;

  int channel_;
  I_Instrument *source_;
  int16_t currentNote_;
  int16_t previousNote_;
  bool legatoNotePending_;
  ParameterRefs params_;
  InsertEffectPool insertEffectPool_;
  InsertEffectSlot insertEffectSlots_[InsertSlotCount];
  int insertTypes_[InsertSlotCount];
  int insertPositions_[InsertSlotCount];
  etl::vector<InsertEffectSlot *, InsertSlotCount> preInsertChain_;
  etl::vector<InsertEffectSlot *, InsertSlotCount> postInsertChain_;
  BaseWidthState bandpassState_[2];
  BandpassCoefficientState bandpassCoefficients_;
  StandardFilterState standardFilterState_[2];
  FilterMorphState filterMorph_;
  StageLFOMasks stageLfoMasks_;
  // Command-owned state survives IRT in place. Reset new command state only
  // in resetCommandState(), never in source/envelope/processing-stage startup.
  CommandValueState ampCommand_;
  CommandValueState panCommand_;
  CommandValueState filterCutoffCommand_;
  CommandValueState filterResonanceCommand_;
  PitchCommandState pitchCommand_;
  // A TLN command override belongs to the command lifecycle, not the ADSR
  // timer.
  uint16_t gateCommandSlicesRemaining_ = 0;
  bool gateCommandActive_ = false;
  float appliedPitchMultiplier_ = 1.0f;
  bool pitchMultiplierApplied_ = false;
  ChannelLFOBank lfoBank_;
  VoiceADSR ampEnvelope_;
  VoiceADSR filterEnvelope_;
  uint16_t triggerReleaseSlicesRemaining_;
  bool releasePending_;
};

#endif
