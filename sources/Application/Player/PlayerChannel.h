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

#ifndef _PLAYER_CHANNEL_H_
#define _PLAYER_CHANNEL_H_

#include "Application/Audio/AudioSample.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Mixer/MixBus.h"
#include "AudioVoice.h"
#include "Services/Audio/AudioModule.h"

class PlayerChannel : public AudioModule {
public:
  PlayerChannel(int index);
  virtual ~PlayerChannel();
  virtual bool Render(AudioSample *buffer, int samplecount);
  void StartInstrument(I_Instrument *instr, unsigned char note,
                       InstrumentStartMode mode, uint8_t velocity = 127);
  void ReleaseChannel();
  void ReleaseInstrument();
  void StopInstrument();
  bool ProcessCommand(TrackerCommand command, ushort value);
  I_Instrument *GetInstrument();
  void SetMute(bool muted);
  inline bool IsMuted() const { return muted_; }
  void SetMixBus(int i);
  void Reset();

private:
  int index_;
  AudioVoice voice_;
  I_Instrument *midiInstrument_;
  bool muted_;
  int busIndex_;
  MixBus *mixBus_;
};

#endif
