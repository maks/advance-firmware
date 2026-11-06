/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef SAMPLE_WAVEFORM_H
#define SAMPLE_WAVEFORM_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace SampleWaveform {
constexpr uint32_t SamplesPerColumn = 32;

// Build an approximate RMS overview directly from the loaded sample. Bound
// reads by the display resolution, not the sample length. Small/zoomed windows
// still use every frame. As before, stereo waveforms show the first channel.
inline void Build(const int16_t *samples, uint32_t channels, uint32_t start,
                  uint32_t end, uint8_t *output, uint32_t columns,
                  uint8_t height) {
  if (columns == 0) {
    return;
  }
  std::fill_n(output, columns, uint8_t{0});
  if (!samples || channels == 0 || end <= start) {
    return;
  }

  const uint32_t span = end - start;
  uint32_t first = 0;
  for (uint32_t column = 0; column < columns; ++column) {
    // Ceil boundaries match floor(relativeFrame * columns / span), including
    // empty columns when there are fewer frames than display columns.
    const uint32_t next = static_cast<uint32_t>(
        (static_cast<uint64_t>(column + 1) * span + columns - 1) / columns);
    const uint32_t count = std::min(next - first, SamplesPerColumn);
    if (count != 0) {
      uint32_t sumSquares = 0;
      for (uint32_t i = 0; i < count; ++i) {
        // Include both ends of each column and spread the remaining reads
        // across its full interval rather than only inspecting its beginning.
        const uint32_t offset =
            count == 1
                ? 0
                : static_cast<uint32_t>(static_cast<uint64_t>(i) *
                                        (next - first - 1) / (count - 1));
        const int32_t quant =
            samples[static_cast<size_t>(start + first + offset) * channels] >>
            8;
        sumSquares += quant * quant;
      }
      const float rms = std::sqrt(static_cast<float>(sumSquares) / count);
      output[column] = static_cast<uint8_t>(rms * height / 128.0f);
    }
    first = next;
  }
}
} // namespace SampleWaveform

#endif
