/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "DattorroReverbOutputEffect.h"

#include "Services/Audio/AudioDriver.h"
#include "System/Console/n_assert.h"
#include "config/MemorySections.h"

namespace {

#if defined(ADV) && !defined(HOST_TEST) && !defined(WASM)
// The fixed reverb owns these exact DSP buffers directly; there is no shared
// output-effect arena or runtime reservation step.
__attribute__((aligned(32))) __attribute__((section(".RAM_D2"))) uint8_t
    gDattorroInternalMemory[DattorroReverbOutputEffect::kInternalMemorySize];
__attribute__((aligned(32))) __attribute__((section(".RAM_EXT"))) uint8_t
    gDattorroExternalMemory[DattorroReverbOutputEffect::kExternalMemorySize];
#else
alignas(32) uint8_t
    gDattorroInternalMemory[DattorroReverbOutputEffect::kInternalMemorySize];
alignas(32) uint8_t
    gDattorroExternalMemory[DattorroReverbOutputEffect::kExternalMemorySize];
#endif

int Clamp(int value, int minimum, int maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

int32_t MapDiffuseMacroCoefficient(int32_t diffuse,
                                   int32_t canonicalCoefficient) {
  diffuse = Clamp(diffuse, DattorroReverbOutputEffect::MinimumParameterValue,
                  DattorroReverbOutputEffect::MaximumParameterValue);
  if (diffuse <= DattorroReverbOutputEffect::DefaultDiffuse) {
    const int32_t rounding = DattorroReverbOutputEffect::DefaultDiffuse / 2;
    return (diffuse * canonicalCoefficient + rounding) /
           DattorroReverbOutputEffect::DefaultDiffuse;
  }

  const int32_t diffuseAboveDefault =
      diffuse - DattorroReverbOutputEffect::DefaultDiffuse;
  const int32_t rangeAboveDefault =
      DattorroReverbOutputEffect::MaximumParameterValue -
      DattorroReverbOutputEffect::DefaultDiffuse;
  const int32_t coefficientAboveDefault =
      DattorroReverbOutputEffect::MaximumDiffusion - canonicalCoefficient;
  return canonicalCoefficient + (diffuseAboveDefault * coefficientAboveDefault +
                                 rangeAboveDefault / 2) /
                                    rangeAboveDefault;
}

} // namespace

DattorroReverbOutputEffect::DattorroReverbOutputEffect()
    : OutputEffect(&variables_),
      preDelay_(FourCC::VarReverbPreDelay, DefaultPreDelay),
      inputTone_(FourCC::VarReverbInputTone, DefaultInputTone),
      diffuse_(FourCC::VarReverbDiffuse, static_cast<int>(DefaultDiffuse)),
      preDiffusion1_(FourCC::VarReverbPreDiffusion1, DefaultPreDiffusion1),
      preDiffusion2_(FourCC::VarReverbPreDiffusion2, DefaultPreDiffusion2),
      decay_(FourCC::VarReverbDecay, DefaultDecay),
      decayDiffusion_(FourCC::VarReverbDecayDiffusion, DefaultDecayDiffusion),
      tailTone_(FourCC::VarReverbTailTone, DefaultTailTone),
      modulationRate_(FourCC::VarReverbModulationRate, DefaultModulationRate),
      modulationDepth_(FourCC::VarReverbModulationDepth,
                       DefaultModulationDepth) {
  variables_.push_back(&preDelay_);
  variables_.push_back(&inputTone_);
  // Keep the macro before its component controls so persisted component
  // overrides are restored after the macro has set its canonical ratios.
  variables_.push_back(&diffuse_);
  variables_.push_back(&preDiffusion1_);
  variables_.push_back(&preDiffusion2_);
  variables_.push_back(&decay_);
  variables_.push_back(&decayDiffusion_);
  variables_.push_back(&tailTone_);
  variables_.push_back(&modulationRate_);
  variables_.push_back(&modulationDepth_);

  ObserveParameter(preDelay_);
  ObserveParameter(inputTone_);
  ObserveParameter(diffuse_);
  ObserveParameter(preDiffusion1_);
  ObserveParameter(preDiffusion2_);
  ObserveParameter(decay_);
  ObserveParameter(decayDiffusion_);
  ObserveParameter(tailTone_);
  ObserveParameter(modulationRate_);
  ObserveParameter(modulationDepth_);

  const bool initialized = implementation_.Initialize(
      gDattorroInternalMemory, sizeof(gDattorroInternalMemory),
      gDattorroExternalMemory, sizeof(gDattorroExternalMemory));
  NAssert(initialized);
  ApplyParameters();
  Reset();
}

void DattorroReverbOutputEffect::ApplyDiffuseMacro() {
  const int32_t diffuse = diffuse_.GetInt();
  preDiffusion1_.SetInt(
      MapDiffuseMacroCoefficient(diffuse, DefaultPreDiffusion1));
  preDiffusion2_.SetInt(
      MapDiffuseMacroCoefficient(diffuse, DefaultPreDiffusion2));
  decayDiffusion_.SetInt(
      MapDiffuseMacroCoefficient(diffuse, DefaultDecayDiffusion));
}

void DattorroReverbOutputEffect::Update(Observable &observable,
                                        I_ObservableData *data) {
  if (&observable == static_cast<Observable *>(&diffuse_)) {
    ApplyDiffuseMacro();
  }
  OutputEffect::Update(observable, data);
}

void DattorroReverbOutputEffect::ApplyParameters() {
  implementation_.Configure({
      .preDelay_ = preDelay_.GetInt(),
      .inputTone_ = inputTone_.GetInt(),
      .preDiffusion1_ = preDiffusion1_.GetInt(),
      .preDiffusion2_ = preDiffusion2_.GetInt(),
      .decay_ = decay_.GetInt(),
      .decayDiffusion_ = decayDiffusion_.GetInt(),
      .tailTone_ = tailTone_.GetInt(),
      .modulationRate_ = modulationRate_.GetInt(),
      .modulationDepth_ = modulationDepth_.GetInt(),
  });
}

void DattorroReverbOutputEffect::Reset() { implementation_.Reset(); }

int32_t DattorroReverbOutputEffect::GetTailFrameCount() const {
  return implementation_.GetTailFrameCount();
}

PICOTRACKER_FAST_CODE("dattorro_reverb_output_effect_render")
void DattorroReverbOutputEffect::Render(const AudioSample *input,
                                        AudioSample *output, int sampleCount) {
  NAssert(input != nullptr);
  NAssert(output != nullptr);
  NAssert(sampleCount >= 0);
  NAssert(sampleCount <= MAX_SAMPLE_COUNT);
  implementation_.Render(input, output, static_cast<int32_t>(sampleCount));
}
