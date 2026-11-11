/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _AUDIO_RECORDING_H_
#define _AUDIO_RECORDING_H_

#include <cstdint>

#define LINEIN_GAIN_MINDB -6
#define LINEIN_GAIN_MAXDB 24
#define MIC_GAIN_MINDB 0  // can be negitive but mic level is already very low
#define MIC_GAIN_MAXDB 20 // per TLV320 datasheet max gain is 20dB

enum RecordSource { AllOff, LineIn, Mic, USBIn };

bool StartRecording(const char *filename, uint8_t threshold,
                    uint32_t milliseconds);
void StopRecording();
void CancelRecording();
void RequestStopRecording();
bool WaitForRecordingStop(uint32_t timeoutMs);
void StartMonitoring();
void StopMonitoring();
void SetInputSource(RecordSource source);
void SetLineInGain(int32_t gainDb);
void SetMicGain(int32_t gainDb);
bool IsRecordingActive();
bool IsSavingRecording();
uint8_t GetSavingProgressPercent();
bool DidLastRecordingCaptureAudio();

#endif
