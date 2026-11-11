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

#ifndef _AUDIO_FILE_STREAMER_H_
#define _AUDIO_FILE_STREAMER_H_

#include "Application/Audio/AudioSample.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Model/Project.h"
#include "Externals/etl/include/etl/string.h"
#include "Services/Audio/AudioModule.h"
#include "System/FileSystem/FileSystem.h"
#include <atomic>

#define SINGLE_CYCLE_MAX_SAMPLE_SIZE 1024

enum AudioFileStreamerMode { AFSM_STOPPED, AFSM_PLAYING, AFSM_LOOPING };

class AudioFileStreamer : public AudioModule {
public:
  AudioFileStreamer();
  virtual ~AudioFileStreamer();
  virtual bool Render(AudioSample *buffer, int samplecount);
  // Automatically detects single cycle waveforms
  bool Start(const char *name, int startSample = 0, bool looping = false);
  void Stop();
  bool IsPlaying();

protected:
  std::atomic<AudioFileStreamerMode> mode_{AFSM_STOPPED};
  etl::string<PFILENAME_SIZE - 1> name_;
  WavFile wav_;
  float position_;
  Project *project_;

  // Sample rate conversion
  int fileSampleRate_;
  int systemSampleRate_;
  float speed_; // Speed factor for sample rate conversion

  // Static buffer for single cycle waveforms. Single-cycle playback is
  // mono-only, so this stores up to SINGLE_CYCLE_MAX_SAMPLE_SIZE frames.
  static short singleCycleBuffer_[SINGLE_CYCLE_MAX_SAMPLE_SIZE];
  short *singleCycleData_; // Pointer to the current single cycle data

  // For matching oscillator mode in SampleInstrument
  float referencePitch_; // Reference pitch in Hz (C3 = 130.81 Hz)

public:
  void SetProject(Project *project) { project_ = project; }

private:
  void stopPlayback();
};

#endif
