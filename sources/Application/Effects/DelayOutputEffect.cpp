/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "DelayOutputEffect.h"

#include "Application/Player/SyncMaster.h"
#include "Services/Audio/AudioDriver.h"
#include "System/Console/n_assert.h"
#include "config/MemorySections.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr AudioSample kFeedbackTailThreshold = 0.001f;
static_assert(DelayOutputEffect::TicksPerStep == SyncMaster::SlicesPerStep,
              "Delay time units must match sequencer audio ticks");

#if defined(ADV) && !defined(HOST_TEST) && !defined(WASM)
// The fixed delay owns this complete history buffer directly; there is no
// shared output-effect arena or runtime reservation step.
__attribute__((aligned(32))) __attribute__((section(".RAM_EXT")))
uint8_t gDelayMemory[DelayOutputEffect::kExternalMemorySize];
#else
alignas(32) uint8_t gDelayMemory[DelayOutputEffect::kExternalMemorySize];
#endif

uint8_t ToDelayTime(int time) {
  if (time < DelayOutputEffect::MinimumTime) {
    return DelayOutputEffect::MinimumTime;
  }
  if (time > DelayOutputEffect::MaximumTime) {
    return DelayOutputEffect::MaximumTime;
  }
  return static_cast<uint8_t>(time);
}

uint32_t ToDelayFrames(uint8_t time) {
  SyncMaster *sync = SyncMaster::GetInstance();
  const float tickFrameCount = sync ? sync->GetTickSampleCount() : 0.0f;
  if (tickFrameCount <= 0.0f) {
    return 1;
  }

  // TIME is measured in audio ticks so every selectable value corresponds to
  // a sequencer render boundary without passing tempo data through the generic
  // output-effect API.
  uint32_t delayFrames =
      static_cast<uint32_t>((tickFrameCount * static_cast<float>(time)) + 0.5f);
  if (delayFrames < 1) {
    delayFrames = 1;
  } else if (delayFrames > DelayOutputEffect::kBufferFrameCount) {
    delayFrames = DelayOutputEffect::kBufferFrameCount;
  }
  return delayFrames;
}

uint8_t ToDelayFeedback(int feedback) {
  if (feedback < 0) {
    return 0;
  }
  if (feedback > DelayOutputEffect::MaximumFeedback) {
    return DelayOutputEffect::MaximumFeedback;
  }
  return static_cast<uint8_t>(feedback);
}

AudioSample ToFeedbackGain(uint8_t feedback) {
  return static_cast<AudioSample>(feedback) /
         static_cast<AudioSample>(DelayOutputEffect::FeedbackScale);
}

AudioSample SaturateFeedbackSample(AudioSample sample) {
  // New input can overlap a feedback repeat, so keep the combined value within
  // the valid audio range before storing it for the next repetition.
  if (sample < kAudioSampleMin) {
    return kAudioSampleMin;
  }
  if (sample > kAudioSampleMax) {
    return kAudioSampleMax;
  }
  return sample;
}

} // namespace

DelayOutputEffect::DelayOutputEffect()
    : OutputEffect(&variables_), time_(FourCC::VarDelayTime, DefaultTime),
      feedback_(FourCC::VarDelayFeedback, DefaultFeedback) {
  delayBuffer_ = reinterpret_cast<StereoFrame *>(gDelayMemory);
  variables_.push_back(&time_);
  variables_.push_back(&feedback_);

  ObserveParameter(time_);
  ObserveParameter(feedback_);
  Reset();
}

void DelayOutputEffect::ApplyParameters() {
  SetParameters(ToDelayTime(GetTime()), ToDelayFeedback(GetFeedback()));
}

void DelayOutputEffect::SetParameters(uint8_t time, uint8_t feedback) {
  timeValue_ = time;
  feedbackGain_ = ToFeedbackGain(feedback);
  delayFrames_ = ToDelayFrames(timeValue_);
  UpdateFeedbackTailRepeatCount();
}

void DelayOutputEffect::RefreshDelayFrames() {
  const uint32_t delayFrames = ToDelayFrames(timeValue_);
  if (delayFrames == delayFrames_) {
    return;
  }

  delayFrames_ = delayFrames;
}

void DelayOutputEffect::UpdateFeedbackTailRepeatCount() {
  feedbackTailRepeatCount_ = 1;
  if (feedbackGain_ <= kAudioSampleZero) {
    return;
  }

  AudioSample level = feedbackGain_;
  while (level >= kFeedbackTailThreshold &&
         feedbackTailRepeatCount_ < UINT16_MAX) {
    ++feedbackTailRepeatCount_;
    level *= feedbackGain_;
  }
}

void DelayOutputEffect::Reset() {
  if (delayBuffer_ == nullptr) {
    return;
  }
  memset(delayBuffer_, 0, kExternalMemorySize);
  writeFrame_ = 0;
}

int32_t DelayOutputEffect::GetTailFrameCount() const {
  const uint32_t delayFrames = ToDelayFrames(timeValue_);
  const uint64_t tailFrames =
      static_cast<uint64_t>(delayFrames) * feedbackTailRepeatCount_;
  return static_cast<int32_t>(
      tailFrames < static_cast<uint64_t>(INT32_MAX) ? tailFrames : INT32_MAX);
}

PICOTRACKER_FAST_CODE("delay_output_effect_render")
void DelayOutputEffect::Render(const AudioSample *input, AudioSample *output,
                               int samplecount) {
  NAssert(delayBuffer_ != nullptr);
  NAssert(input != nullptr);
  NAssert(output != nullptr);
  NAssert(samplecount >= 0);
  NAssert(samplecount <= MAX_SAMPLE_COUNT);

  RefreshDelayFrames();
  for (int32_t frame = 0; frame < samplecount; ++frame) {
    const uint32_t readFrame =
        writeFrame_ >= delayFrames_
            ? writeFrame_ - delayFrames_
            : kBufferFrameCount + writeFrame_ - delayFrames_;
    const StereoFrame delayed = delayBuffer_[readFrame];

    const int32_t sample = frame * 2;
    output[sample] = delayed.left_;
    output[sample + 1] = delayed.right_;
    if (feedbackGain_ > kAudioSampleZero) {
      delayBuffer_[writeFrame_] = {
          .left_ = SaturateFeedbackSample(input[sample] +
                                          delayed.left_ * feedbackGain_),
          .right_ = SaturateFeedbackSample(input[sample + 1] +
                                           delayed.right_ * feedbackGain_),
      };
    } else {
      delayBuffer_[writeFrame_] = {
          .left_ = input[sample],
          .right_ = input[sample + 1],
      };
    }

    ++writeFrame_;
    if (writeFrame_ == kBufferFrameCount) {
      writeFrame_ = 0;
    }
  }
}
