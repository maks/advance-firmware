/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _LINE_IN_MIXER_H_
#define _LINE_IN_MIXER_H_

#include "Application/Audio/AudioSample.h"
#include "Services/Audio/AudioModule.h"
#include "Services/Audio/LineInSource.h"

#include <cstdint>

// Mixes the live line input straight into the master output stream. It is
// mounted on the audio output mixer after the master bus, which keeps the
// input out of the channel effect sends while still putting it in front of the
// render gain, so a mixdown file contains exactly what is heard.
class LineInMixer : public AudioModule {
public:
  LineInMixer();

  // AudioModule
  bool Render(AudioSample *buffer, int samplecount) override;

  void SetSource(LineInSource *source);

  // Unity gain is 1.0. A gain of zero releases the converter.
  void SetGain(AudioSample gain);

  // Peak of what this module mixed into the output, measured after its gain so
  // the meter agrees with what is heard, packed as the other mixers do: left in
  // the high 16 bits, right in the low 16 bits.
  stereosample GetLevels() const { return peakLevel_; }

  // Input and output share a clock. Prime with about 5.8 ms of scheduling
  // reserve at 44.1 kHz plus the next render block, then consume the reserve
  // as needed. Trim stalls above 11.6 ms back to the target.
  static constexpr uint32_t kTargetBacklogFrames = 256;
  static constexpr uint32_t kMaxBacklogFrames = 512;

private:
  bool IsEnabled() const { return gain_ > kAudioSampleZero; }
  static constexpr uint32_t kChunkFrames = 128;

  LineInSource *source_;
  AudioSample gain_;
  stereosample peakLevel_;
  bool primed_ = false;
  int16_t chunk_[kChunkFrames * 2];
};

#endif
