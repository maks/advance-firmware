/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#ifndef _AUDIO_H_
#define _AUDIO_H_

#include "AudioConstants.h"
#include "AudioOut.h"
#include "AudioSettings.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/T_Factory.h"
#include "config/StringLimits.h"

class LineInSource;

class Audio : public T_Factory<Audio> {
public:
  static constexpr size_t MaxAudioOuts = 4;

  Audio(AudioSettings &settings);
  virtual ~Audio();
  virtual void Init() = 0;
  virtual void Close() = 0;
  virtual int GetSampleRate() { return kAudioSampleRateHz; };
  virtual int GetMixerVolume() { return 100; };
  virtual void SetMixerVolume(int volume){};

  // Live converter frames for the line input mix, or nullptr when the platform
  // has no line input to offer.
  virtual LineInSource *GetLineInSource() { return nullptr; };

  const char *GetAudioAPI();
  const char *GetAudioDevice();
  int GetAudioBufferSize();
  int GetAudioPreBufferCount();
  void AddOutput(AudioOut &out);
  AudioOut *GetFirstOutput();
  etl::vector<AudioOut *, MaxAudioOuts> &Outputs() { return outputs_; }

protected:
  AudioSettings settings_;

  etl::vector<AudioOut *, MaxAudioOuts> outputs_;

private:
  etl::string<STRING_AUDIO_API_MAX> audioAPI_;
  etl::string<STRING_AUDIO_DEVICE_MAX> audioDevice_;
  int audioBufferSize_;
  int preBufferCount_;
};
#endif
