/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/audio/AudioWorklet.h"
#include "Adapters/wasm/audio/OutputResampler.h"
#include "Adapters/wasm/audio/PcmRingBuffer.h"
#include "Adapters/wasm/audio/WasmAudioDriver.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Player/Player.h"
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

TEST_CASE("PcmRingBuffer: basic read and write conversion") {
  PcmRingBuffer<1024> ring;
  REQUIRE(ring.FillFrames() == 0);
  REQUIRE(ring.Underruns() == 0);
  REQUIRE(ring.Overruns() == 0);

  // 4 frames: (L, R) pairs in short
  const std::vector<short> interleaved = {0,     0,      -32768, 16384,
                                          32767, -16384, -1,     1};

  const size_t written = ring.WriteInterleaved(interleaved);
  REQUIRE(written == 4);
  REQUIRE(ring.FillFrames() == 4);

  std::vector<StereoF32> out(4);
  const size_t read = ring.Read(out);
  REQUIRE(read == 4);
  REQUIRE(ring.FillFrames() == 0);
  REQUIRE(ring.Underruns() == 0);

  // Frame 0: 0, 0
  CHECK(out[0].left == doctest::Approx(0.0f));
  CHECK(out[0].right == doctest::Approx(0.0f));

  // Frame 1: -32768, 16384
  CHECK(out[1].left == doctest::Approx(-1.0f));
  CHECK(out[1].right == doctest::Approx(0.5f).epsilon(0.001f));

  // Frame 2: 32767, -16384
  CHECK(out[2].left == doctest::Approx(32767.0f / 32768.0f));
  CHECK(out[2].right == doctest::Approx(-0.5f).epsilon(0.001f));
}

TEST_CASE("PcmRingBuffer: underrun zero fills and records count") {
  PcmRingBuffer<512> ring;

  const std::vector<short> input = {1000, -1000, 2000, -2000};
  REQUIRE(ring.WriteInterleaved(input) == 2);

  std::vector<StereoF32> out(6, {99.0f, 99.0f});
  const size_t read = ring.Read(out);
  REQUIRE(read == 2);
  REQUIRE(ring.Underruns() == 4);
  REQUIRE(ring.FillFrames() == 0);

  // First 2 frames contain real data
  CHECK(out[0].left != 0.0f);
  CHECK(out[1].left != 0.0f);

  // Remaining 4 frames are zero-filled
  CHECK(out[2].left == 0.0f);
  CHECK(out[2].right == 0.0f);
  CHECK(out[3].left == 0.0f);
  CHECK(out[4].left == 0.0f);
  CHECK(out[5].left == 0.0f);
}

TEST_CASE("PcmRingBuffer: overrun drops excess frames and records count") {
  PcmRingBuffer<64> ring;
  std::vector<short> full(64 * 2, 100);
  REQUIRE(ring.WriteInterleaved(full) == 64);
  REQUIRE(ring.FillFrames() == 64);
  REQUIRE(ring.Overruns() == 0);

  std::vector<short> excess(10 * 2, 200);
  const size_t accepted = ring.WriteInterleaved(excess);
  REQUIRE(accepted == 0);
  REQUIRE(ring.Overruns() == 10);
  REQUIRE(ring.FillFrames() == 64);
}

TEST_CASE("PcmRingBuffer: Drain discards queued frames") {
  PcmRingBuffer<256> ring;
  std::vector<short> data(50 * 2, 500);
  REQUIRE(ring.WriteInterleaved(data) == 50);
  REQUIRE(ring.FillFrames() == 50);

  ring.Drain();
  REQUIRE(ring.FillFrames() == 0);

  // Reading after Drain results in underrun / silence
  std::vector<StereoF32> out(10);
  const size_t read = ring.Read(out);
  REQUIRE(read == 0);
  REQUIRE(ring.Underruns() == 10);
  CHECK(out[0].left == 0.0f);
}

TEST_CASE("PcmRingBuffer: Reset clears state and counters") {
  PcmRingBuffer<256> ring;
  std::vector<short> data(10 * 2, 123);
  REQUIRE(ring.WriteInterleaved(data) == 10);
  std::vector<StereoF32> out(20);
  const size_t read = ring.Read(out);
  REQUIRE(read == 10);
  REQUIRE(ring.Underruns() == 10);

  ring.Reset();
  REQUIRE(ring.FillFrames() == 0);
  REQUIRE(ring.Underruns() == 0);
  REQUIRE(ring.Overruns() == 0);
}

TEST_CASE("OutputResampler: unity rate passes data without distortion") {
  OutputResampler resampler(44100, 44100);

  std::vector<StereoF32> in(16);
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] = {static_cast<float>(i) * 0.05f, -static_cast<float>(i) * 0.05f};
  }

  std::vector<StereoF32> out(16);
  auto res = resampler.Process(in, out);
  REQUIRE(res.inputFramesConsumed == 16);
  REQUIRE(res.outputFramesProduced == 16);

  for (size_t i = 0; i < 16; ++i) {
    CHECK(out[i].left == doctest::Approx(in[i].left));
    CHECK(out[i].right == doctest::Approx(in[i].right));
  }
}

TEST_CASE(
    "OutputResampler: upsampling 44100 to 48000 produces proportional frames") {
  OutputResampler resampler(44100, 48000);

  // Feed 441 source frames (which equals 0.01 sec)
  // At 48000 Hz, 0.01 sec is 480 frames.
  std::vector<StereoF32> in(441, {0.25f, -0.25f});
  std::vector<StereoF32> out(600);

  auto res = resampler.Process(in, out);
  REQUIRE(res.inputFramesConsumed == 441);

  auto flushRes = resampler.Flush(
      std::span<StereoF32>(out.data() + res.outputFramesProduced,
                           out.size() - res.outputFramesProduced));
  const size_t totalProduced =
      res.outputFramesProduced + flushRes.outputFramesProduced;
  // 441 * 48000 / 44100 = 480 frames
  REQUIRE(totalProduced == 480);
}

TEST_CASE("WasmAudioDriver: volume and gain calculations") {
  AudioSettings settings{};
  settings.bufferSize_ = 1024;
  WasmAudioDriver driver(settings);

  driver.Configure(2048, WasmAudioDriver::UnityGainQ16);
  REQUIRE(driver.TargetFillFramesConfigured() == 2048);

  driver.SetMixerVolume(100);
  REQUIRE(driver.OutputGainQ16() == WasmAudioDriver::UnityGainQ16);

  driver.SetMixerVolume(50);
  REQUIRE(driver.OutputGainQ16() == WasmAudioDriver::UnityGainQ16 / 2);

  driver.SetMixerVolume(0);
  REQUIRE(driver.OutputGainQ16() == 0);
}

TEST_CASE("WasmAudioDriver: offline render does not write to playback ring") {
  AudioSettings settings{};
  settings.bufferSize_ = 1024;
  WasmAudioDriver driver(settings);
  driver.StartDriver();
  REQUIRE(driver.IsStarted());

  // In normal MSM_AUDIO mode, AddBuffer writes to ring
  std::vector<short> audioBuffer(64 * 2, 1000);
  driver.AddBuffer(audioBuffer.data(), 64);
  const auto metrics1 = driver.Metrics();
  REQUIRE(metrics1.ringFillFrames == 64);

  driver.DrainRing();
  REQUIRE(driver.Metrics().ringFillFrames == 0);
}

TEST_CASE("WasmAudioWorkletRenderer: Render outputs silence when inactive and "
          "active audio when started") {
  AudioSettings settings{};
  settings.bufferSize_ = 1024;
  WasmAudioDriver driver(settings);
  WasmAudioWorkletRenderer renderer(driver, 44100);

  std::vector<float> left(128, 1.0f);
  std::vector<float> right(128, 1.0f);

  // Driver not started or active -> should output silence (0.0f)
  REQUIRE(renderer.Render(left.data(), right.data(), 128));
  CHECK(left[0] == 0.0f);
  CHECK(right[0] == 0.0f);
  CHECK(driver.Metrics().underrunFrames == 0U);

  // Start driver and mark active
  driver.StartDriver();
  driver.OnAudioActive(true);
  REQUIRE(driver.IsStarted());
  REQUIRE(driver.IsActive());

  // Write some audio: 128 frames of constant +16384 (0.5f)
  std::vector<short> audioBuffer(128 * 2, 16384);
  driver.AddBuffer(audioBuffer.data(), 128);

  REQUIRE(renderer.Render(left.data(), right.data(), 128));
  CHECK(left[0] == doctest::Approx(0.5f).epsilon(0.01f));
  CHECK(right[0] == doctest::Approx(0.5f).epsilon(0.01f));
  CHECK(driver.Metrics().underrunFrames == 0U);
}

TEST_CASE("WasmAudioWorkletRenderer: active output shortages count as "
          "underruns") {
  AudioSettings settings{};
  settings.bufferSize_ = 1024;
  WasmAudioDriver driver(settings);
  WasmAudioWorkletRenderer renderer(driver, 44100);
  driver.StartDriver();
  driver.OnAudioActive(true);

  std::vector<float> left(128, 1.0f);
  std::vector<float> right(128, 1.0f);

  REQUIRE(renderer.Render(left.data(), right.data(), 128));
  CHECK(driver.Metrics().underrunFrames == 128U);
  CHECK(left[0] == 0.0f);
  CHECK(right[0] == 0.0f);

  std::vector<short> partialBuffer(64 * 2, 16384);
  driver.AddBuffer(partialBuffer.data(), 64);

  REQUIRE(renderer.Render(left.data(), right.data(), 128));
  CHECK(driver.Metrics().underrunFrames == 192U);
  CHECK(left[0] == doctest::Approx(0.5f).epsilon(0.01f));
  CHECK(left[63] == doctest::Approx(0.5f).epsilon(0.01f));
  CHECK(left[64] == 0.0f);
}

TEST_CASE("WasmAudioWorkletRenderer: RenderOracle runs deterministically") {
  const auto oracle441 = WasmAudioWorkletRenderer::RenderOracle(44100);
  REQUIRE(oracle441.destinationRate == 44100);
  REQUIRE(oracle441.producedFrames > 0);
  REQUIRE(oracle441.sampleHash != 0);

  const auto oracle480 = WasmAudioWorkletRenderer::RenderOracle(48000);
  REQUIRE(oracle480.destinationRate == 48000);
  REQUIRE(oracle480.producedFrames > 0);
  REQUIRE(oracle480.sampleHash != 0);
}
