/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Services/Audio/AudioRecording.h"

bool StartRecording(const char *, uint8_t, uint32_t) { return false; }
void StopRecording() {}
void CancelRecording() {}
void RequestStopRecording() {}
bool WaitForRecordingStop(uint32_t) { return true; }
void StartMonitoring() {}
void StopMonitoring() {}
void SetInputSource(RecordSource) {}
void SetLineInGain(int32_t) {}
void SetMicGain(int32_t) {}
bool IsRecordingActive() { return false; }
bool IsSavingRecording() { return false; }
uint8_t GetSavingProgressPercent() { return 0; }
bool DidLastRecordingCaptureAudio() { return false; }
