/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#include "Application/Mixer/LineInMixer.h"
#include "doctest/doctest.h"

using doctest::Approx;

namespace {

constexpr uint32_t kRingFrames = 8192;
constexpr int kBlockFrames = 8;

// Stand in for the platform capture ring. Test cases decide how many frames
// are waiting before each render pass.
class FakeLineInSource : public LineInSource {
public:
  uint32_t availableFrames() const override { return produced_ - consumed_; }

  uint32_t readFrames(int16_t *dst, uint32_t maxFrames) override {
    uint32_t frames = availableFrames();
    if (frames > maxFrames) {
      frames = maxFrames;
    }
    for (uint32_t i = 0; i < frames; ++i) {
      const uint32_t index = (consumed_ + i) % kRingFrames;
      dst[i * 2] = samples_[index * 2];
      dst[i * 2 + 1] = samples_[index * 2 + 1];
    }
    consumed_ += frames;
    return frames;
  }

  void skipFrames(uint32_t frames) override {
    const uint32_t available = availableFrames();
    consumed_ += frames > available ? available : frames;
  }

  void setCaptureRequested(bool requested) override {
    captureRequested = requested;
    requestCount++;
  }

  // Pushes one stereo frame as the converter writes it, right channel first.
  void push(int16_t left, int16_t right) {
    const uint32_t index = (produced_ % kRingFrames) * 2;
    samples_[index] = right;
    samples_[index + 1] = left;
    produced_++;
  }

  // Fills the ring with a known counting pattern, oldest frame first, the way
  // a primed capture would. Values stay inside 16 bits by wrapping.
  void pushFrames(uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
      const int16_t value =
          static_cast<int16_t>((produced_ + baseValue_) % 2000);
      push(value, value);
    }
  }

  // Value the frame at absolute position index carries.
  int16_t valueAt(uint32_t index) const {
    return static_cast<int16_t>((index + baseValue_) % 2000);
  }

  uint32_t baseValue_ = 0;

  // Silent frames, to bring the queue up to the target backlog.
  void prime(uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
      push(0, 0);
    }
  }

  uint32_t produced_ = 0;
  uint32_t consumed_ = 0;
  bool captureRequested = false;
  int requestCount = 0;
  int16_t samples_[kRingFrames * 2] = {0};
};

} // namespace

TEST_CASE("Line in mixer stays silent and idle while muted") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);

  AudioSample buffer[4] = {0.25f, -0.25f, 0.5f, -0.5f};
  CHECK_FALSE(mixer.Render(buffer, 2));
  CHECK(buffer[0] == Approx(0.25f));
  CHECK(buffer[3] == Approx(-0.5f));
  CHECK_FALSE(source.captureRequested);
  CHECK(mixer.GetLevels() == 0);
}

TEST_CASE("Line in mixer releases the converter when muted again") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);

  mixer.SetGain(0.5f);
  CHECK(source.captureRequested);
  CHECK(source.requestCount == 1);

  mixer.SetGain(0.0f);
  CHECK_FALSE(source.captureRequested);
  CHECK(source.requestCount == 2);

  // A second call with the same state must not touch the hardware again.
  mixer.SetGain(0.0f);
  CHECK(source.requestCount == 2);
}

TEST_CASE("Line in mixer starts from the freshest audio when un-muted") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);

  // Audio captured while the input was muted must not be replayed.
  source.push(1234, 1234);
  source.push(5678, 5678);

  mixer.SetGain(1.0f);
  CHECK(source.availableFrames() == 0);

  AudioSample buffer[2] = {0.75f, -0.75f};
  CHECK_FALSE(mixer.Render(buffer, 1));
  CHECK(buffer[0] == Approx(0.0f));
  CHECK(mixer.GetLevels() == 0);
}

TEST_CASE("Line in mixer waits for the target backlog before mixing") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(1.0f);

  // One frame short of what the module wants to see in hand.
  source.pushFrames(LineInMixer::kTargetBacklogFrames + kBlockFrames - 1);

  AudioSample buffer[kBlockFrames * 2];
  for (auto &sample : buffer) {
    sample = 0.5f;
  }

  CHECK_FALSE(mixer.Render(buffer, kBlockFrames));
  for (const auto &sample : buffer) {
    CHECK(sample == Approx(0.0f));
  }
  // Nothing may be consumed while waiting, or the backlog never fills up.
  CHECK(source.availableFrames() ==
        LineInMixer::kTargetBacklogFrames + kBlockFrames - 1);
}

TEST_CASE("Line in mixer consumes a primed ring oldest first") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(0.5f);

  // A little over the target backlog of known frames, oldest first.
  constexpr uint32_t queued =
      LineInMixer::kTargetBacklogFrames + 2 * kBlockFrames;
  source.pushFrames(queued);

  AudioSample buffer[kBlockFrames * 2] = {0};
  REQUIRE(mixer.Render(buffer, kBlockFrames));

  const AudioSample scale = 0.5f / kAudioPcmScale;
  for (int frame = 0; frame < kBlockFrames; ++frame) {
    const AudioSample expected = source.valueAt(frame) * scale;
    CHECK(buffer[frame * 2] == Approx(expected));
    CHECK(buffer[frame * 2 + 1] == Approx(expected));
  }

  // The oldest frames go first and the rest, target backlog included, waits for
  // the next block. Nothing is reordered or skipped.
  CHECK(source.availableFrames() == queued - kBlockFrames);
}

TEST_CASE("Line in mixer packs peak level left above right") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(1.0f);

  // Peaks at the oldest end of the queue, where the next block is taken from.
  source.push(16384, -8192);
  source.push(-4096, 32767);
  source.prime(LineInMixer::kTargetBacklogFrames + kBlockFrames - 2);

  AudioSample buffer[kBlockFrames * 2] = {0};
  REQUIRE(mixer.Render(buffer, kBlockFrames));

  // Peaks are absolute, so negative swings register, and left sits above
  // right.
  const auto levels = static_cast<uint32_t>(mixer.GetLevels());
  CHECK((levels >> 16) == 16384u);
  CHECK((levels & 0xFFFF) == 32767u);
}

TEST_CASE("Line in mixer trims backlog beyond the latency limit") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(1.0f);

  // A render stall left far more queued than the module is willing to delay by.
  constexpr uint32_t backlog =
      LineInMixer::kMaxBacklogFrames + kBlockFrames + 500;
  source.pushFrames(backlog);

  AudioSample buffer[kBlockFrames * 2] = {0};
  REQUIRE(mixer.Render(buffer, kBlockFrames));

  // Latency is pulled back to the target once the block is taken, and the block
  // carries the audio that sat at that point in the queue, not the newest
  // frames.
  CHECK(source.availableFrames() == LineInMixer::kTargetBacklogFrames);

  const uint32_t oldestKept =
      backlog - LineInMixer::kTargetBacklogFrames - kBlockFrames;
  for (int frame = 0; frame < kBlockFrames; ++frame) {
    const AudioSample expected = source.valueAt(oldestKept + frame);
    CHECK(buffer[frame * 2] == Approx(expected / kAudioPcmScale));
  }
}

TEST_CASE("Line in mixer reports the level after its own gain") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(0.25f);

  // The same signal the unity gain case measures, behind a quarter fader.
  source.push(16384, -8192);
  source.prime(LineInMixer::kTargetBacklogFrames + kBlockFrames - 1);

  AudioSample buffer[kBlockFrames * 2] = {0};
  REQUIRE(mixer.Render(buffer, kBlockFrames));

  const auto levels = static_cast<uint32_t>(mixer.GetLevels());
  CHECK((levels >> 16) == 4096u);
  CHECK((levels & 0xFFFF) == 2048u);
}

TEST_CASE("Line in mixer uses its reserve across scheduling jitter") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(1.0f);
  source.pushFrames(LineInMixer::kTargetBacklogFrames + 128);

  AudioSample buffer[256];
  REQUIRE(mixer.Render(buffer, 128));
  const uint32_t nextFrame = source.consumed_;

  // The next render runs before the next 128 captured frames have arrived.
  // Use the reserve instead of inserting silence or skipping captured audio.
  REQUIRE(mixer.Render(buffer, 128));
  CHECK(buffer[0] == Approx(source.valueAt(nextFrame) / kAudioPcmScale));
  CHECK(source.availableFrames() == LineInMixer::kTargetBacklogFrames - 128);

  source.pushFrames(256);
  REQUIRE(mixer.Render(buffer, 128));
  CHECK(source.availableFrames() == LineInMixer::kTargetBacklogFrames);
}

TEST_CASE("Line in mixer maintains its backlog across variable render blocks") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(1.0f);
  source.pushFrames(LineInMixer::kTargetBacklogFrames);

  AudioSample buffer[1875 * 2];
  constexpr uint32_t blockSizes[] = {1, 64, 128, 192, 1875};
  for (uint32_t pass = 0; pass < 50; ++pass) {
    for (uint32_t frames : blockSizes) {
      const uint32_t nextFrame = source.consumed_;
      source.pushFrames(frames);
      REQUIRE(mixer.Render(buffer, frames));
      CHECK(source.availableFrames() == LineInMixer::kTargetBacklogFrames);
      CHECK(buffer[0] == Approx(source.valueAt(nextFrame) / kAudioPcmScale));
    }
  }
}

TEST_CASE("Line in mixer primes again after capture is interrupted") {
  FakeLineInSource source;
  LineInMixer mixer;
  mixer.SetSource(&source);
  mixer.SetGain(1.0f);
  source.pushFrames(LineInMixer::kTargetBacklogFrames + kBlockFrames);
  AudioSample buffer[kBlockFrames * 2];
  REQUIRE(mixer.Render(buffer, kBlockFrames));

  source.skipFrames(source.availableFrames());
  CHECK_FALSE(mixer.Render(buffer, kBlockFrames));
  source.pushFrames(kBlockFrames);
  CHECK_FALSE(mixer.Render(buffer, kBlockFrames));
  CHECK(source.availableFrames() == kBlockFrames);

  source.pushFrames(LineInMixer::kTargetBacklogFrames);
  CHECK(mixer.Render(buffer, kBlockFrames));
}

TEST_CASE("Line in mixer releases its old source when replaced") {
  FakeLineInSource oldSource;
  FakeLineInSource newSource;
  LineInMixer mixer;
  mixer.SetSource(&oldSource);
  mixer.SetGain(1.0f);
  REQUIRE(oldSource.captureRequested);
  mixer.SetSource(&newSource);
  CHECK_FALSE(oldSource.captureRequested);
  CHECK(newSource.captureRequested);
}
