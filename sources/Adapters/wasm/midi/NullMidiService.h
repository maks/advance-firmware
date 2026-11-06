/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef NULL_MIDI_SERVICE_H
#define NULL_MIDI_SERVICE_H

#include "Services/Midi/MidiService.h"

class NullMidiService final : public MidiService {
public:
  NullMidiService() = default;
  ~NullMidiService() override = default;
};

#endif
