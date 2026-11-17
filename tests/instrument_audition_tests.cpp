/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Application/Instruments/FM6Instrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Instruments/VoiceParameters.h"
#include "Application/Instruments/picoSwarmInstrument.h"
#include "Application/Model/Config.h"
#include "Application/Player/Player.h"
#include "boundary_test_utils.h"

namespace {

class AuditionTestSamplePool : public SamplePool {
public:
  void Reset() override { count_ = 0; }

  bool CheckSampleFits(uint32_t sampleSize) override {
    (void)sampleSize;
    return true;
  }

  uint32_t GetAvailableSampleStorageSpace() override { return 0; }

  bool unloadSample(uint32_t index) override {
    (void)index;
    return false;
  }

protected:
  bool loadSample(const char *name) override {
    (void)name;
    return false;
  }
};

class SamplePoolInstallGuard {
public:
  SamplePoolInstallGuard() : previous_(SamplePool::GetInstance()) {
    static AuditionTestSamplePool pool;
    SamplePool::Install(&pool);
  }

  ~SamplePoolInstallGuard() { SamplePool::Install(previous_); }

private:
  SamplePool *previous_;
};

} // namespace

TEST_CASE("Instrument audition defaults on and restores a saved off setting") {
  SUBCASE("default is on") {
    BoundaryTestEnvironment env;
    Config *config = Config::GetInstance();

    REQUIRE(config != nullptr);
    CHECK(config->IsInstrumentAuditionEnabled());
  }

  SUBCASE("saved off setting is restored") {
    BoundaryTestEnvironment env;
    env.fs.AddFile("/.config.xml",
                   "<CONFIG><INSTAUDITION VALUE=\"0\"/></CONFIG>");
    Config *config = Config::GetInstance();

    REQUIRE(config != nullptr);
    CHECK_FALSE(config->IsInstrumentAuditionEnabled());
  }
}

TEST_CASE("Direct note audition remains active until every channel stops") {
  DirectNoteChannelState state;

  CHECK_FALSE(state.IsActive());
  state.StartChannel(1);
  state.StartChannel(6);
  CHECK(state.IsActive());

  state.StopChannel(1);
  CHECK(state.IsActive());
  state.StopChannel(6);
  CHECK_FALSE(state.IsActive());

  state.StartChannel(3);
  state.Clear();
  CHECK_FALSE(state.IsActive());
}

TEST_CASE("Sample auditions level, mode, and playback points") {
  SamplePoolInstallGuard poolGuard;
  SampleSourceInstrument instrument;
  const FourCC soundParameters[] = {
      FourCC::SampleSourceInstrumentLevel,
      FourCC::SampleSourceInstrumentLoopMode,
      FourCC::SampleSourceInstrumentStart,
      FourCC::SampleSourceInstrumentLoopStart,
      FourCC::SampleSourceInstrumentEnd,
  };

  for (FourCC parameter : soundParameters) {
    CHECK(instrument.ShouldAuditionParameterChange(parameter));
  }
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::SampleSourceInstrumentSample));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::SampleSourceInstrumentRootNote));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::SampleSourceInstrumentTable));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::SampleSourceInstrumentTableAutomation));
}

TEST_CASE("Original Sample auditions every parameter from volume to end") {
  SamplePoolInstallGuard poolGuard;
  SampleInstrument instrument;
  const FourCC soundParameters[] = {
      FourCC::SampleInstrumentVolume,
      FourCC::SampleInstrumentPan,
      FourCC::SampleInstrumentRootNote,
      FourCC::SampleInstrumentFineTune,
      FourCC::SampleInstrumentCrushVolume,
      FourCC::SampleInstrumentCrush,
      FourCC::SampleInstrumentDownsample,
      FourCC::SampleInstrumentFilterCutOff,
      FourCC::SampleInstrumentFilterResonance,
      FourCC::SampleInstrumentFilterType,
      FourCC::SampleInstrumentFilterMode,
      FourCC::SampleInstrumentInterpolation,
      FourCC::SampleInstrumentLoopMode,
      FourCC::SampleInstrumentStart,
      FourCC::SampleInstrumentLoopStart,
      FourCC::SampleInstrumentEnd,
  };

  for (FourCC parameter : soundParameters) {
    CHECK(instrument.ShouldAuditionParameterChange(parameter));
  }
  CHECK_FALSE(
      instrument.ShouldAuditionParameterChange(FourCC::SampleInstrumentSample));
  CHECK_FALSE(
      instrument.ShouldAuditionParameterChange(FourCC::SampleInstrumentTable));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::SampleInstrumentTableAutomation));
}

TEST_CASE("SID auditions every displayed sound parameter") {
  SIDInstrument instrument;
  const FourCC soundParameters[] = {
      FourCC::SIDInstrumentChipModel,
      FourCC::SIDInstrument1Volume,
      FourCC::SIDInstrumentWaveform,
      FourCC::SIDInstrument2Waveform,
      FourCC::SIDInstrument3Waveform,
      FourCC::SIDInstrumentPitchOffset,
      FourCC::SIDInstrument2PitchOffset,
      FourCC::SIDInstrument3PitchOffset,
      FourCC::SIDInstrumentPulseWidth,
      FourCC::SIDInstrument2PulseWidth,
      FourCC::SIDInstrument3PulseWidth,
      FourCC::SIDInstrumentVSync,
      FourCC::SIDInstrument2VSync,
      FourCC::SIDInstrument3VSync,
      FourCC::SIDInstrumentRingModulator,
      FourCC::SIDInstrument2RingModulator,
      FourCC::SIDInstrument3RingModulator,
      FourCC::SIDInstrumentADSR,
      FourCC::SIDInstrument2ADSR,
      FourCC::SIDInstrument3ADSR,
      FourCC::SIDInstrumentFilterOn,
      FourCC::SIDInstrument2FilterOn,
      FourCC::SIDInstrument3FilterOn,
      FourCC::SIDInstrument1FilterCut,
      FourCC::SIDInstrument1FilterResonance,
      FourCC::SIDInstrument1FilterMode,
  };

  for (FourCC parameter : soundParameters) {
    CHECK(instrument.ShouldAuditionParameterChange(parameter));
  }
  CHECK_FALSE(
      instrument.ShouldAuditionParameterChange(FourCC::SIDInstrumentTable));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::SIDInstrumentTableAutomation));
}

TEST_CASE("FM6 auditions algorithm, operator, and global parameter changes") {
  FM6Instrument instrument;
  const FourCC soundParameters[] = {
      FourCC::FM6InstrumentOp1Params,    FourCC::FM6InstrumentOp2Params,
      FourCC::FM6InstrumentOp3Params,    FourCC::FM6InstrumentOp4Params,
      FourCC::FM6InstrumentOp5Params,    FourCC::FM6InstrumentOp6Params,
      FourCC::FM6InstrumentGlobalParams,
  };

  for (FourCC parameter : soundParameters) {
    CHECK(instrument.ShouldAuditionParameterChange(parameter));
  }
  CHECK_FALSE(
      instrument.ShouldAuditionParameterChange(FourCC::FM6InstrumentProgram));
  CHECK_FALSE(
      instrument.ShouldAuditionParameterChange(FourCC::FM6InstrumentTable));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::FM6InstrumentTableAutomation));
}

TEST_CASE("picoSwarm auditions detune and mix") {
  picoSwarmInstrument instrument;

  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::picoSwarmInstrumentDetune));
  CHECK(
      instrument.ShouldAuditionParameterChange(FourCC::picoSwarmInstrumentMix));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::picoSwarmInstrumentTable));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::picoSwarmInstrumentTableAutomation));
}

TEST_CASE("Audio Voice auditions every owned parameter") {
  VoiceParameters parameters;
  VoiceParameters::ValueSnapshot snapshot{};

  parameters.CaptureValues(snapshot);
  CHECK_FALSE(parameters.ValuesDifferFrom(snapshot));

  Variable *pan = parameters.FindVariable(FourCC::VoicePan);
  REQUIRE(pan != nullptr);
  pan->SetInt(pan->GetInt() - 1, false);
  CHECK(parameters.ValuesDifferFrom(snapshot));
}

TEST_CASE("MIDI voice CCs scale fields and preserve LFO rules") {
  VoiceParameters voice;
  VoiceParameters::ValueSnapshot displayed{};
  voice.CaptureValues(displayed);
  CHECK_FALSE(voice.ValuesDifferFrom(displayed));
  voice.ApplyMidiCC(74, 0);
  CHECK(voice.ValuesDifferFrom(displayed));
  voice.CaptureValues(displayed);
  CHECK_FALSE(voice.ValuesDifferFrom(displayed));
  CHECK(voice.FindVariable(FourCC::VoiceFilterCutoff)->GetInt() == 0);
  voice.ApplyMidiCC(74, 127);
  CHECK(voice.FindVariable(FourCC::VoiceFilterCutoff)->GetInt() == 255);
  voice.ApplyMidiCC(18, 64);
  CHECK(voice.FindVariable(FourCC::VoiceFilterEnvAmount)->GetInt() == 0);
  voice.ApplyMidiCC(14, 127);
  CHECK(voice.FindVariable(FourCC::VoiceTriggerLength)->GetInt() == 96);
  voice.ApplyMidiCC(30, 127);
  CHECK(voice.GetLFOSettings()->Get(0).target_ == LFOTarget::VoiceAmpRelease);
  voice.ApplyMidiCC(36, 127);
  CHECK(voice.GetLFOSettings()->Get(0).target_ == LFOTarget::None);
  CHECK(voice.GetLFOSettings()->Get(1).target_ == LFOTarget::VoiceAmpRelease);
  voice.ApplyMidiCC(39, 127);
  voice.ApplyMidiCC(40, 127);
  CHECK(voice.GetLFOSettings()->Get(1).syncIndex_ ==
        static_cast<int32_t>(GetLFOSyncDivisionCount()) - 1);
  voice.ApplyMidiCC(41, 127);
  CHECK(voice.GetLFOSettings()->Get(1).depth_ == 255);
  voice.ApplyMidiCC(74, 128);
  CHECK(voice.FindVariable(FourCC::VoiceFilterCutoff)->GetInt() == 255);
}
