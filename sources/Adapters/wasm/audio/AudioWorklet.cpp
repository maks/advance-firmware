/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AudioWorklet.h"

#include "WasmAudioBridge.h"
#include "WasmAudioDriver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

WasmAudioWorkletRenderer::WasmAudioWorkletRenderer(
    WasmAudioDriver &driver, std::uint32_t destinationRate) noexcept
    : driver_(&driver), resampler_(44100U, destinationRate) {
  driver_->SetDestinationRate(destinationRate);
}

void WasmAudioWorkletRenderer::SetDestinationRate(
    std::uint32_t destinationRate) noexcept {
  resampler_ = OutputResampler(44100U, destinationRate);
  if (driver_ != nullptr) {
    driver_->SetDestinationRate(destinationRate);
  }
  sourceOffset_ = 0U;
  sourceCount_ = 0U;
}

void WasmAudioWorkletRenderer::RecordCallback(double callbackMilliseconds,
                                              std::size_t frames) noexcept {
  driver_->RecordCallback(callbackMilliseconds, frames);
}

bool WasmAudioWorkletRenderer::Render(float *left, float *right,
                                      std::size_t frames) noexcept {
  if (left == nullptr || right == nullptr || frames > MaxFramesPerCallback ||
      driver_ == nullptr) {
    return false;
  }
  if (!driver_->IsStarted() || !driver_->IsActive()) {
    std::fill_n(left, frames, 0.0F);
    std::fill_n(right, frames, 0.0F);
    sourceOffset_ = 0U;
    sourceCount_ = 0U;
    resampler_.Reset();
    driver_->DrainRing();
    return true;
  }

  std::array<StereoF32, MaxFramesPerCallback> rendered{};
  std::size_t produced = 0U;
  while (produced < frames) {
    if (sourceOffset_ == sourceCount_ && !Refill()) {
      break;
    }
    const auto result = resampler_.Process(
        std::span<const StereoF32>(source_.data() + sourceOffset_,
                                   sourceCount_ - sourceOffset_),
        std::span<StereoF32>(rendered.data() + produced, frames - produced));
    sourceOffset_ += result.inputFramesConsumed;
    produced += result.outputFramesProduced;
    if (result.inputFramesConsumed == 0U && result.outputFramesProduced == 0U) {
      break;
    }
  }
  driver_->RecordUnderrunFrames(frames - produced);
  const float gain = static_cast<float>(driver_->OutputGainQ16()) /
                     static_cast<float>(WasmAudioDriver::UnityGainQ16);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const StereoF32 value = frame < produced ? rendered[frame] : StereoF32{};
    left[frame] = value.left * gain;
    right[frame] = value.right * gain;
  }
  return true;
}

bool WasmAudioWorkletRenderer::Refill() noexcept {
  sourceOffset_ = 0U;
  sourceCount_ = 0U;
  if (driver_ == nullptr) {
    return false;
  }
  const std::size_t available = driver_->FillFrames();
  if (available == 0U) {
    return false;
  }
  const std::size_t toRead = std::min(source_.size(), available);
  sourceCount_ =
      driver_->ReadFrames(std::span<StereoF32>(source_.data(), toRead));
  return sourceCount_ != 0U;
}

WasmAudioRenderOracle
WasmAudioWorkletRenderer::RenderOracle(std::uint32_t destinationRate) noexcept {
  WasmAudioRenderOracle oracle{};
  oracle.destinationRate = destinationRate;
  if (destinationRate == 0U) {
    return oracle;
  }

  OutputResampler resampler(44100U, destinationRate);
  constexpr std::size_t TestFrames = 1024U;
  std::array<StereoF32, TestFrames> input{};
  for (std::size_t i = 0; i < TestFrames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(TestFrames);
    input[i] = {t * 0.75f, -t * 0.5f};
  }

  std::array<StereoF32, 128U> outputBuffer{};
  std::uint32_t hash = 2166136261U; // FNV-1a offset basis
  float maxPeak = 0.0f;
  std::size_t totalProduced = 0U;

  auto processChunk = [&](std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
      const StereoF32 &frame = outputBuffer[i];
      const float peak = std::max(std::abs(frame.left), std::abs(frame.right));
      if (peak > maxPeak) {
        maxPeak = peak;
      }
      const auto *leftBytes =
          reinterpret_cast<const std::uint8_t *>(&frame.left);
      const auto *rightBytes =
          reinterpret_cast<const std::uint8_t *>(&frame.right);
      for (std::size_t b = 0; b < sizeof(float); ++b) {
        hash = (hash ^ leftBytes[b]) * 16777619U;
        hash = (hash ^ rightBytes[b]) * 16777619U;
      }
    }
    totalProduced += count;
  };

  std::size_t consumed = 0U;
  while (consumed < TestFrames) {
    const auto res =
        resampler.Process(std::span<const StereoF32>(input.data() + consumed,
                                                     TestFrames - consumed),
                          outputBuffer);
    consumed += res.inputFramesConsumed;
    processChunk(res.outputFramesProduced);
    if (res.inputFramesConsumed == 0U && res.outputFramesProduced == 0U) {
      break;
    }
  }

  for (;;) {
    const auto flushRes = resampler.Flush(outputBuffer);
    if (flushRes.outputFramesProduced == 0U) {
      break;
    }
    processChunk(flushRes.outputFramesProduced);
  }

  oracle.producedFrames = static_cast<std::uint32_t>(totalProduced);
  oracle.sampleHash = hash;
  oracle.peakQ15 = static_cast<std::uint32_t>(
      std::clamp(maxPeak * 32767.0f, 0.0f, 32767.0f));
  return oracle;
}

#ifdef __EMSCRIPTEN__
extern "C" bool PicoTracker_Wasm_AudioWorkletProcess(
    int numInputs, const AudioSampleFrame *inputs, int numOutputs,
    AudioSampleFrame *outputs, int numParams, const AudioParamFrame *params,
    void *userData) noexcept {
  (void)numInputs;
  (void)inputs;
  (void)numParams;
  (void)params;
  auto *renderer = static_cast<WasmAudioWorkletRenderer *>(userData);
  if (renderer == nullptr || numOutputs != 1 || outputs == nullptr ||
      outputs[0].numberOfChannels < 2 || outputs[0].samplesPerChannel < 0) {
    return false;
  }
  const std::size_t frames =
      static_cast<std::size_t>(outputs[0].samplesPerChannel);
  float *const planar = outputs[0].data;
  if (planar == nullptr) {
    return false;
  }
  const double callbackStart = emscripten_get_now();
  if (!renderer->Render(planar, planar + frames, frames)) {
    return false;
  }
  WasmAudio_MarkRunning();
  const double callbackEnd = emscripten_get_now();
  renderer->RecordCallback(callbackEnd - callbackStart, frames);
  return true;
}
#endif
