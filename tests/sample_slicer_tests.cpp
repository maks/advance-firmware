/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Application/Views/SampleSlicer.h"
#include "Application/Views/SampleWaveform.h"

TEST_CASE("sample waveform matches full RMS for small uneven windows") {
  int16_t samples[37];
  for (uint32_t i = 0; i < 37; ++i) {
    samples[i] = static_cast<int16_t>((static_cast<int32_t>(i % 9) - 4) * 4096);
  }
  uint8_t output[7];
  SampleWaveform::Build(samples, 1, 3, 36, output, 7, 180);
  uint32_t sums[7] = {};
  uint32_t counts[7] = {};
  for (uint32_t i = 3; i < 36; ++i) {
    const uint32_t column = (i - 3) * 7 / 33;
    const int32_t quant = samples[i] >> 8;
    sums[column] += quant * quant;
    ++counts[column];
  }
  for (uint32_t column = 0; column < 7; ++column) {
    CHECK(output[column] ==
          static_cast<uint8_t>(
              std::sqrt(static_cast<float>(sums[column]) / counts[column]) /
              128.0f * 180));
  }
}

TEST_CASE("sample waveform preserves sparse columns and first stereo channel") {
  const int16_t samples[] = {32767, 0, -32768, 0, 16384, -32768, 0, -32768};
  uint8_t output[6];
  SampleWaveform::Build(samples, 2, 1, 4, output, 6, 180);
  CHECK(output[0] == 180);
  CHECK(output[1] == 0);
  CHECK(output[2] == 90);
  CHECK(output[3] == 0);
  CHECK(output[4] == 0);
  CHECK(output[5] == 0);
}

TEST_CASE("large sample waveform retains amplitude across the full window") {
  static int16_t samples[65536];
  std::fill_n(samples, 32768, int16_t{-32768});
  std::fill_n(samples + 32768, 32768, int16_t{16384});
  uint8_t output[8];
  SampleWaveform::Build(samples, 1, 0, 65536, output, 8, 180);
  for (uint32_t column = 0; column < 8; ++column) {
    CHECK(output[column] == (column < 4 ? 180 : 90));
  }
  std::fill_n(samples, 65536, int16_t{0});
  SampleWaveform::Build(samples, 1, 0, 65536, output, 8, 180);
  for (uint8_t amplitude : output) {
    CHECK(amplitude == 0);
  }
}

TEST_CASE("sample waveform clears invalid input and handles a single frame") {
  const int16_t sample = -32768;
  uint8_t output = 42;
  SampleWaveform::Build(&sample, 1, 0, 1, &output, 1, 180);
  CHECK(output == 180);
  SampleWaveform::Build(nullptr, 1, 0, 1, &output, 1, 180);
  CHECK(output == 0);
  output = 42;
  SampleWaveform::Build(&sample, 0, 0, 1, &output, 1, 180);
  CHECK(output == 0);
  output = 42;
  SampleWaveform::Build(&sample, 1, 1, 1, &output, 1, 180);
  CHECK(output == 0);
  output = 42;
  SampleWaveform::Build(&sample, 1, 2, 1, &output, 1, 180);
  CHECK(output == 0);
  SampleWaveform::Build(nullptr, 0, 0, 0, nullptr, 0, 180);
}

TEST_CASE("shared waveform graph geometry is centered and 660 pixels wide") {
  CHECK(GraphField::DefaultXOffset == 30);
  CHECK(GraphField::BitmapWidth == 660);
  CHECK(SampleSlicer::GraphXOffset == GraphField::DefaultXOffset);
  CHECK(SampleSlicer::GraphWidth == GraphField::BitmapWidth);
  CHECK(SampleSlicer::GraphHeight == GraphField::BitmapHeight);
  CHECK(GraphField::MaxMarkers >= SampleSlicer::MaxSlices + 5);
}

TEST_CASE("slicer preview is handled for graph and slicer-owned focus") {
  CHECK(SampleSlicer::ShouldHandlePreview(true, false, false));
  CHECK(SampleSlicer::ShouldHandlePreview(false, true, false));
  CHECK(SampleSlicer::ShouldHandlePreview(false, false, true));
  CHECK_FALSE(SampleSlicer::ShouldHandlePreview(false, false, false));
}

TEST_CASE("slicer previews projected sample points and loop type focus") {
  CHECK(SampleSlicer::ShouldPreviewFromProjectedFocus(true, false));
  CHECK(SampleSlicer::ShouldPreviewFromProjectedFocus(false, true));
  CHECK_FALSE(SampleSlicer::ShouldPreviewFromProjectedFocus(false, false));
}

TEST_CASE("slicer preview range is only used when slices are disabled") {
  CHECK(SampleSlicer::ShouldUsePreviewRange(false, true));
  CHECK_FALSE(SampleSlicer::ShouldUsePreviewRange(true, true));
  CHECK_FALSE(SampleSlicer::ShouldUsePreviewRange(false, false));
}

TEST_CASE("slicer preview loop start is only shown for loop modes") {
  CHECK(SampleSlicer::ShouldShowPreviewLoopStart(true, true));
  CHECK_FALSE(SampleSlicer::ShouldShowPreviewLoopStart(true, false));
  CHECK_FALSE(SampleSlicer::ShouldShowPreviewLoopStart(false, true));
}

TEST_CASE("embedded slicer automatic playhead is sample source only") {
  CHECK(SampleSlicer::ShouldTrackAutomaticPlayhead(IT_SAMPLE_SOURCE));
  CHECK_FALSE(SampleSlicer::ShouldTrackAutomaticPlayhead(IT_SAMPLE));
  CHECK_FALSE(SampleSlicer::ShouldTrackAutomaticPlayhead(IT_MIDI));
  CHECK_FALSE(SampleSlicer::ShouldTrackAutomaticPlayhead(IT_NONE));
}

TEST_CASE("slicer preview note follows slices before root note") {
  CHECK(SampleSlicer::PreviewNoteForState(true, 48, 2, 67) == 50);
  CHECK(SampleSlicer::PreviewNoteForState(false, 48, 2, 67) == 67);
  CHECK(SampleSlicer::PreviewNoteForState(true, 48, -4, 67) == 48);
  CHECK(SampleSlicer::PreviewNoteForState(false, 48, 2, 200) == 127);
}
