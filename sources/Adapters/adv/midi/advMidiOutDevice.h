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
#ifndef _ADVMIDIDEVICE_H_
#define _ADVMIDIDEVICE_H_

#include "FreeRTOS.h"
#include "Services/Midi/MidiOutDevice.h"
#include "semphr.h"

class advMidiOutDevice : public MidiOutDevice {
public:
  advMidiOutDevice(const char *name);
  virtual bool Init();
  virtual void Close();
  virtual bool Start();
  virtual void Stop();
  // Adapter-only helper to send immediately
  void SendImmediate(MidiMessage &msg);

protected:
  virtual void SendMessage(MidiMessage &);
  virtual void SendRawBytes(const uint8_t *data, uint8_t len);

private:
  StaticSemaphore_t transmitMutexStorage_{};
  SemaphoreHandle_t transmitMutex_;
};
#endif
