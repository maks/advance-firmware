/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#ifndef _AUDIO_H_
#define _AUDIO_H_

#include "Services/Audio/AudioConstants.h"

class Audio {
public:
  static Audio *GetInstance() {
    static Audio instance;
    return &instance;
  }
  int GetSampleRate() { return kAudioSampleRateHz; }
};

#endif
