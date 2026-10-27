/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_DECENT_SAMPLER_PRESET_H
#define PICOTRACKER_DECENT_SAMPLER_PRESET_H

#include "System/FileSystem/PathUtils.h"
#include <cstdint>

struct DecentSamplerZone {
  char path_[PFILENAME_SIZE];
  uint8_t rootNote_;
  uint8_t lowNote_;
  uint8_t highNote_;
  uint8_t lowVelocity_;
  uint8_t highVelocity_;
  uint32_t startFrame_;
  uint32_t endFrame_; // exclusive; zero means end of file
};

struct DecentSamplerPreset {
  static constexpr uint16_t MaxZones = 128;
  uint16_t zoneCount_ = 0;
  DecentSamplerZone zones_[MaxZones]{};
};

enum class DecentSamplerPresetError : uint8_t {
  None,
  OpenFailed,
  MalformedXml,
  UnsupportedFeature,
  InvalidMapping,
  MissingSample,
  TooManyZones,
  OverlappingZones,
  UnsupportedWav,
};

struct DecentSamplerPresetResult {
  DecentSamplerPresetError error_ = DecentSamplerPresetError::None;
  uint32_t line_ = 0;
  const char *message_ = "";
  explicit operator bool() const {
    return error_ == DecentSamplerPresetError::None;
  }
};

// Reads only the immutable key/velocity mapping subset. The caller owns the
// destination storage; this routine does not allocate memory.
DecentSamplerPresetResult LoadDecentSamplerPreset(
    const char *presetPath, DecentSamplerPreset &preset);

#endif
