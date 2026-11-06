/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "LineInMixer.h"

#include "System/Profiler/DWTProfiler.h"
#include "config/MemorySections.h"

#include <algorithm>

namespace {

constexpr float kPcmToFloat = 1.0f / kAudioPcmScale;

} // namespace

LineInMixer::LineInMixer()
    : source_(nullptr), gain_(kAudioSampleZero), peakLevel_(0) {}

void LineInMixer::SetSource(LineInSource *source) {
  if (source == source_) {
    return;
  }

  const bool wasRequested = IsEnabled();
  if (source_ != nullptr && wasRequested) {
    source_->setCaptureRequested(false);
  }
  source_ = source;
  primed_ = false;
  if (source_ == nullptr) {
    peakLevel_ = 0;
    return;
  }

  if (wasRequested) {
    source_->setCaptureRequested(true);
  }
}

void LineInMixer::SetGain(AudioSample gain) {
  const bool wasEnabled = IsEnabled();
  gain_ = std::max(kAudioSampleZero, gain);
  const bool isEnabled = IsEnabled();

  if (wasEnabled == isEnabled || source_ == nullptr) {
    return;
  }

  source_->setCaptureRequested(isEnabled);
  primed_ = false;
  if (!isEnabled) {
    peakLevel_ = 0;
    return;
  }

  // Start from the freshest audio instead of replaying whatever queued up
  // while the input was muted.
  source_->skipFrames(source_->availableFrames());
}

PICOTRACKER_FAST_CODE("line_in_mixer_render")
bool LineInMixer::Render(AudioSample *buffer, int samplecount) {
  if ((source_ == nullptr) || (gain_ <= kAudioSampleZero) ||
      (samplecount <= 0)) {
    peakLevel_ = 0;
    return false;
  }

  const uint32_t needed = static_cast<uint32_t>(samplecount);
  uint32_t available = source_->availableFrames();

  // Prime once, then allow scheduling jitter to use the reserve. Requiring the
  // full backlog on every block would turn a small delay into a silent block.
  if (available < needed ||
      (!primed_ && available < needed + kTargetBacklogFrames)) {
    primed_ = false;
    std::fill_n(buffer, needed * 2, kAudioSampleZero);
    peakLevel_ = 0;
    return false;
  }
  primed_ = true;

  // Bound latency after a stall by dropping back to the target reserve.
  if (available > needed + kMaxBacklogFrames) {
    source_->skipFrames(available - needed - kTargetBacklogFrames);
  }

  AudioSample *dst = buffer;

  AudioSample peakL = 0;
  AudioSample peakR = 0;
  uint32_t remaining = needed;

  // From here on the frames are consumed oldest first, which is what keeps the
  // stream in one piece.
  while (remaining > 0) {
    const uint32_t chunk = std::min(remaining, kChunkFrames);
    const uint32_t got = source_->readFrames(chunk_, chunk);
    if (got == 0) {
      break;
    }

    for (uint32_t i = 0; i < got * 2; i += 2) {
      // The converter reaches the reader with the channels the other way
      // round, the same way the record path rotates them before writing a file.
      const AudioSample left =
          static_cast<AudioSample>(chunk_[i + 1]) * kPcmToFloat;
      const AudioSample right =
          static_cast<AudioSample>(chunk_[i]) * kPcmToFloat;

      const AudioSample mixedL = left * gain_;
      const AudioSample mixedR = right * gain_;

      *dst++ = mixedL;
      *dst++ = mixedR;

      // Metered after the fader, as every other mixer strip is.
      const AudioSample absL = std::abs(mixedL);
      const AudioSample absR = std::abs(mixedR);
      peakL = std::max(peakL, absL);
      peakR = std::max(peakR, absR);
    }

    remaining -= got;
  }

  if (remaining > 0) {
    primed_ = false;
    std::fill_n(dst, remaining * 2, kAudioSampleZero);
  }

  const uint32_t mixed = needed - remaining;
  if (mixed == 0) {
    peakLevel_ = 0;
    return false;
  }

  peakLevel_ =
      static_cast<int>(std::clamp(peakL, kAudioSampleZero, kAudioSampleMax) *
                       kAudioPcmScale)
          << 16 |
      static_cast<int>(std::clamp(peakR, kAudioSampleZero, kAudioSampleMax) *
                       kAudioPcmScale);

  return true;
}
