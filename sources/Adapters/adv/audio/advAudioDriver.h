/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */
#ifndef _ADVAUDIO_DRIVER_H_
#define _ADVAUDIO_DRIVER_H_

#include "Foundation/T_Singleton.h"
#include "Services/Audio/AudioConstants.h"
#include "Services/Audio/AudioDriver.h"

#define MINI_BLANK_SIZE 256 // 16-bit samples

class advAudioDriver : public AudioDriver {
public:
  advAudioDriver(AudioSettings &settings);
  virtual ~advAudioDriver();

  // Sound implementation
  virtual bool InitDriver();
  virtual void CloseDriver();
  virtual bool StartDriver();
  virtual void StopDriver();
  void AddBuffer(short *buffer, int samplecount,
                 bool silenceOutput = false) override;
  virtual int GetPlayedBufferPercentage();
  virtual int GetSampleRate() { return kAudioSampleRateHz; };
  virtual bool Interlaced() { return true; };

  // Additional
  void OnChunkDone();
  void SetVolume(int v);
  int GetVolume();
  virtual double GetStreamTime();
  static void IRQHandler();
  static void BufferNeeded();

private:
  static advAudioDriver *instance_;

  AudioSettings settings_;
  static const int16_t miniBlank_[MINI_BLANK_SIZE];

  uint32_t startTime_;
};
#endif
