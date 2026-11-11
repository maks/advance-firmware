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

#include "AudioFileStreamer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Config.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/System.h"
#include "System/io/Status.h"
#include <string.h>
#include <vector>

// Initialize the static buffer for single cycle waveforms
short AudioFileStreamer::singleCycleBuffer_[SINGLE_CYCLE_MAX_SAMPLE_SIZE] = {0};

AudioFileStreamer::AudioFileStreamer() {
  mode_.store(AFSM_STOPPED, std::memory_order_relaxed);
  position_ = 0;
  fileSampleRate_ = 44100;   // Default
  systemSampleRate_ = 44100; // Default
  speed_ = 1.0f;
  project_ = NULL;
  singleCycleData_ = NULL;
  referencePitch_ = 261.63f; // C4 = 261.63 Hz (using C4 to compensate for how
                             // its actually what we call C3 in pT)
};

AudioFileStreamer::~AudioFileStreamer() { Stop(); };

bool AudioFileStreamer::Start(const char *name, int startSample, bool looping) {
  Trace::Debug("Starting to stream:%s from sample %d", name, startSample);
  Stop();
  if (!name) {
    Trace::Error("AudioFileStreamer: null filename");
    return false;
  }

  name_ = name;
  position_ = (startSample > 0) ? float(startSample) : 0.0f;

  Trace::Log("", "wave open:%s", name_.c_str());
  auto res = wav_.Open(name_.c_str());
  if (!res) {
    Trace::Error("Failed to open streaming of file:%s", name_.c_str());
    return false;
  }

  // Get sample rate information and calculate speed factor
  fileSampleRate_ = wav_.GetSampleRate(-1);
  systemSampleRate_ = Audio::GetInstance()->GetSampleRate();
  int channels = wav_.GetChannelCount(-1);
  long size = wav_.GetSize(-1);

  // Calculate the speed factor for sample rate conversion
  float ratio;

  if (looping) {
    // For single cycle waveforms, calculate speed based on the reference
    // pitch this matches how SampleInstrument plays in oscillator mode
    float cyclesPerSecond = referencePitch_;
    float samplesPerCycle = size;
    float samplesPerSecond = cyclesPerSecond * samplesPerCycle;

    // Calculate the speed factor for the reference pitch
    ratio = (float)samplesPerSecond / (float)systemSampleRate_;

    Trace::Debug(
        "AudioFileStreamer: Using reference pitch for single cycle waveform. "
        "Pitch: %.2f Hz, Samples: %ld, Ratio: %.6f",
        referencePitch_, size, ratio);
  } else {
    // Standard sample rate conversion for normal samples
    ratio = (float)fileSampleRate_ / (float)systemSampleRate_;
    Trace::Debug(
        "AudioFileStreamer: Using file sample rate for playback. Ratio: %.6f",
        ratio);
  }

  speed_ = ratio;
  Trace::Debug("AudioFileStreamer: File '%s' - Sample Rate: %d Hz, Channels: "
               "%d",
               name_.c_str(), fileSampleRate_, channels);
  Trace::Debug("Size: %ld samples, Speed: %d", size, (int)speed_);

  // Load the entire buffer for single cycle waveforms
  if (looping) {
    Trace::Log("FileStreamer", "mode: looping");

    // Get channel count before using it
    int channels = wav_.GetChannelCount(-1);
    if (channels != 1 || size > SINGLE_CYCLE_MAX_SAMPLE_SIZE) {
      Trace::Error("AudioFileStreamer: Invalid single cycle source (%ld "
                   "frames, %d channels)",
                   size, channels);
      Stop();
      return false;
    }

    // For safety, load the waveform in smaller chunks to avoid buffer
    // overflow FLASH_PAGE_SIZE  and the assertion in  WavFile::GetBuffer
    // requires size < FLASH_PAGE_SIZE/2, so we use 64 as a safe chunk size
    const int SAFE_CHUNK_SIZE = 64; // in samples
    int remainingSize = size;
    int currentPos = 0;

    while (remainingSize > 0) {
      // Calculate the chunk size for this iteration
      int chunkSize =
          (remainingSize > SAFE_CHUNK_SIZE) ? SAFE_CHUNK_SIZE : remainingSize;

      // Load this chunk
      if (!wav_.GetBuffer(currentPos, chunkSize)) {
        Trace::Error("Failed to load chunk at position %d, size %d", currentPos,
                     chunkSize);
        break;
      }

      // Copy the data to our static buffer
      short *srcBuffer = (short *)wav_.GetSampleBuffer(-1);
      if (srcBuffer) {
        // Calculate destination position in our buffer
        short *destPos = singleCycleBuffer_ + (currentPos * channels);

        // Copy this chunk to our static buffer
        int bytesToCopy = chunkSize * channels * sizeof(short);
        memcpy(destPos, srcBuffer, bytesToCopy);
        // Trace::Debug(
        //     "Loaded chunk of single cycle waveform: pos=%d, size = % d "
        //     "(%d bytes)",
        //     currentPos, chunkSize, bytesToCopy);
      } else {
        Trace::Error("Failed to get sample buffer for chunk at position %d",
                     currentPos);
        break;
      }

      // Move to the next chunk
      currentPos += chunkSize;
      remainingSize -= chunkSize;
    }

    // Set the pointer to our buffer
    if (remainingSize == 0) {
      singleCycleData_ = singleCycleBuffer_;
      Trace::Debug(
          "Successfully loaded entire single cycle waveform: %ld samples",
          size);
    } else {
      // We didn't load the entire waveform
      Trace::Error("Failed to load entire single cycle waveform");
    }
  }

  // Once we were able to open the file, set the mode under mixer lock so
  // Render() observes the fully initialized state coherently.
  MixerService::GetInstance()->Lock();
  mode_.store(looping ? AFSM_LOOPING : AFSM_PLAYING, std::memory_order_release);
  MixerService::GetInstance()->Unlock();

  return true;
};

void AudioFileStreamer::Stop() {
  MixerService::GetInstance()->Lock();
  mode_.store(AFSM_STOPPED, std::memory_order_release);
  MixerService::GetInstance()->Unlock();
  wav_.Close();
  Trace::Debug("Streaming stopped");
};

void AudioFileStreamer::stopPlayback() {
  mode_.store(AFSM_STOPPED, std::memory_order_release);
}

bool AudioFileStreamer::IsPlaying() {
  const AudioFileStreamerMode currentMode =
      mode_.load(std::memory_order_acquire);
  return (currentMode == AFSM_PLAYING || currentMode == AFSM_LOOPING);
}

bool AudioFileStreamer::Render(AudioSample *buffer, int samplecount) {
  // See if we're playing
  const AudioFileStreamerMode currentMode =
      mode_.load(std::memory_order_acquire);
  if (currentMode == AFSM_STOPPED) {
    return false;
  }
  // look if we have the file loaded
  if (!wav_.IsOpen()) {
    Trace::Error("Failed to open streaming of file:%s", name_.c_str());
    stopPlayback();
    return false;
  }
  // We are playing a valid file
  long size = wav_.GetSize(-1);
  int channelCount = wav_.GetChannelCount(-1);

  // Clear the output buffer
  memset(buffer, 0, samplecount * 2 * sizeof(AudioSample));

  // Get preview volume from project
  Variable *v = project_->FindVariable(FourCC::VarPreviewVolume);
  int previewVol = v->GetInt();

  // Apply logarithmic scaling to match human hearing perception
  // Using a formula that gives a more natural volume curve: volume =
  // (value/100)^2 This gives more fine control at lower volumes
  float normalizedVol = (float)previewVol / 100.0f;
  float logVol = normalizedVol * normalizedVol; // Quadratic curve

  // Convert to fixed point
  AudioSample volume = logVol;

  AudioSample *dst = buffer;

  // Special handling for single cycle waveforms in looping mode
  if (mode_ == AFSM_LOOPING) {
    // Use our static buffer that we've already loaded with the entire waveform
    if (!singleCycleData_) {
      Trace::Error("AudioFileStreamer: Single cycle buffer is null");
      stopPlayback();
      return false;
    }

    // Use the static buffer as our source
    short *src = singleCycleData_;

    // Process each output sample
    float pos = position_;
    float cycleSize = (float)size;

    for (int i = 0; i < samplecount; i++) {
      // Calculate the source position, ensuring we wrap within the cycle
      int sourcePos = (int)pos;
      if (sourcePos >= size) {
        sourcePos = sourcePos % size;
      }

      // Get the current sample
      short *currentSample = src + (sourcePos * channelCount);

      // Use linear interpolation between samples for smoother playback
      // Calculate the fractional part for interpolation
      float frac = pos - (float)sourcePos;
      // Get the next sample position for interpolation, with proper wrapping
      int nextPos = sourcePos + 1;
      if (nextPos >= size) {
        nextPos = 0; // Wrap around to the beginning of the cycle
      }

      short *nextSample = src + (nextPos * channelCount);

      if (channelCount == 2) {
        // Stereo sample with interpolation
        AudioSample left1 =
            static_cast<AudioSample>(currentSample[0]) / kAudioPcmScale;
        AudioSample left2 =
            static_cast<AudioSample>(nextSample[0]) / kAudioPcmScale;
        AudioSample leftSample = left1 + (frac * (left2 - left1));

        AudioSample right1 =
            static_cast<AudioSample>(currentSample[1]) / kAudioPcmScale;
        AudioSample right2 =
            static_cast<AudioSample>(nextSample[1]) / kAudioPcmScale;
        AudioSample rightSample = right1 + (frac * (right2 - right1));

        // Apply volume and swap to match reversed hardware wiring
        *dst++ = rightSample * volume;
        *dst++ = leftSample * volume;
      } else {
        // Mono sample with interpolation
        AudioSample sample1 =
            static_cast<AudioSample>(currentSample[0]) / kAudioPcmScale;
        AudioSample sample2 =
            static_cast<AudioSample>(nextSample[0]) / kAudioPcmScale;
        AudioSample sample = sample1 + (frac * (sample2 - sample1));

        // Apply volume and duplicate to both channels
        AudioSample v = sample * volume;
        *dst++ = v;
        *dst++ = v;
      }

      // Move to the next position and wrap around if needed
      pos += speed_;
      while (pos >= cycleSize) {
        pos -= cycleSize;
      }
    }

    // Update the position for the next render call
    position_ = pos;
    return true;
  }

  // Standard playback for normal samples
  int bufferSize = 64; // Small, safe buffer size

  // Check if we're near the end of the file
  if (position_ + (samplecount * speed_) >= size) {
    // We'll reach the end during this render call
    int remainingSamples = size - position_;
    if (remainingSamples <= 0) {
      stopPlayback();
      return false;
    }
  }

  // Read the samples from the file - just once at the current position
  if (!wav_.GetBuffer((int)position_, bufferSize)) {
    Trace::Error("AudioFileStreamer: Failed to get buffer at position %d",
                 (int)position_);
    stopPlayback();
    return false;
  }

  // Get the sample buffer
  short *src = (short *)wav_.GetSampleBuffer(-1);
  if (!src) {
    Trace::Error("AudioFileStreamer: GetSampleBuffer returned null");
    stopPlayback();
    return false;
  }

  // Process each output sample
  float pos = position_;

  for (int i = 0; i < samplecount; i++) {
    // Check if we need to read more samples
    if ((int)pos >= (int)position_ + bufferSize - 1) {
      // We've moved past our buffer, need to read more
      position_ = (int)pos;

      // Check if we've reached the end of the file
      if (position_ >= size - 1) {
        stopPlayback();
        return false;
      }

      // Read the next buffer
      if (!wav_.GetBuffer((int)position_, bufferSize)) {
        Trace::Error("AudioFileStreamer: Failed to get buffer at position %d",
                     (int)position_);
        stopPlayback();
        return false;
      }

      // Update the source pointer
      src = (short *)wav_.GetSampleBuffer(-1);
      if (!src) {
        Trace::Error("AudioFileStreamer: GetSampleBuffer returned null");
        stopPlayback();
        return false;
      }

      // Adjust position to be relative to the new buffer
      pos = position_;
    }

    // Calculate the source position and fractional part
    int sourcePos = (int)(pos - position_);
    float frac = pos - (position_ + sourcePos);

    // Get the current and next sample for interpolation
    short *currentSample = src + (sourcePos * channelCount);
    short *nextSample = currentSample + channelCount;

    // Linear interpolation between samples
    AudioSample left1 =
        static_cast<AudioSample>(currentSample[0]) / kAudioPcmScale;
    AudioSample left2 =
        static_cast<AudioSample>(nextSample[0]) / kAudioPcmScale;
    AudioSample leftSample = left1 + (frac * (left2 - left1));
    AudioSample rightSample;

    if (channelCount == 2) {
      AudioSample right1 =
          static_cast<AudioSample>(currentSample[1]) / kAudioPcmScale;
      AudioSample right2 =
          static_cast<AudioSample>(nextSample[1]) / kAudioPcmScale;
      rightSample = right1 + (frac * (right2 - right1));
    } else {
      rightSample = leftSample;
    }
    // Apply volume and swap to match reversed hardware wiring
    *dst++ = rightSample * volume;
    *dst++ = leftSample * volume;

    // Move to the next source position based on the sample rate ratio
    pos += speed_;
  }

  // Update the position for the next render call
  position_ = pos;

  // If we've reached the end of the file, stop playback
  if (position_ >= size) {
    stopPlayback();
  }

  return true;
}
