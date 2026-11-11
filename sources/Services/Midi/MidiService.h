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

#ifndef _MIDI_SERVICE_H_
#define _MIDI_SERVICE_H_

#include "ControlSurface/ApcMiniMk2Surface.h"
#include "ControlSurface/ControlSurfaceTypes.h"
#include "ControlSurface/IControlSurface.h"
#include "ControlSurface/LaunchpadMiniMk3Surface.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/Observable.h"
#include "Foundation/T_Factory.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "MidiInDevice.h"
#include "MidiOutDevice.h"
#include "System/Timer/Timer.h"
#include <string>

#define MIDI_MAX_BUFFERS 20

class Project;

class MidiService : public T_Factory<MidiService>, public I_Observer {

public:
  MidiService();
  virtual ~MidiService();

  bool Init();
  void Close();
  bool Start();
  void Stop();

  //! player notification
  void OnPlayerStart();
  void OnPlayerStop();
  void RegisterActiveChannel(uint8_t channel);

  //! Queues a MidiMessage to the current time chunk
  void QueueMessage(MidiMessage &);
  void SendImmediate(MidiMessage &);
  void SendRawImmediate(const uint8_t *data, uint8_t len);
  void SendControlSurfaceImmediate(MidiMessage &);
  void SendControlSurfaceRawImmediate(const uint8_t *data, uint8_t len);

  //! Time chunk trigger
  void Trigger();
  void AdvancePlayQueue();

  //! Flush current queue to the output
  void Flush();

  //! Handle MIDI transport messages
  void OnMidiStart();
  void OnMidiStop();
  void OnMidiClock();
  void RefreshControlSurfaceSelection();
  void ReloadControlSurface();
  void LoadProjectSettings(const Project &project);
  // MIDI channel and program values are zero-based at the service boundary.
  void SetInputProgramForChannel(int midiChannel, uint8_t program);
  bool IsInputProgramSelected(int midiChannel, uint8_t program) const;
  bool HasControlSurfaceSongWindow() const;
  uint32_t GetControlSurfaceSongWindowOffset() const;
  uint32_t GetControlSurfaceSongWindowRowCount() const;

protected:
  etl::vector<MidiInDevice *, 2> inList_;
  etl::vector<MidiOutDevice *, 2> outList_;
  etl::vector<MidiInDevice *, 2> activeInDevices_;
  MidiOutDevice *usbControlSurfaceOutDevice_;

  virtual void Update(Observable &o, I_ObservableData *d);
  void onAudioTick();
  virtual void registerControlSurfaceInputs(IControlSurface &surface);
  void registerUsbControlSurfaceOutDevice(MidiOutDevice &device);

  // start the selected midi device
  void startDevice();

  // stop the selected midi device
  void stopDevice();

private:
  void flushOutQueue();
  void updateActiveDevicesList(unsigned short config);
  void updateActiveInputDevicesList(unsigned short config);
  void configureControlSurface(int surfaceSelection);
  void reconcileControlSurfaceSelection();
  bool isUsbOutReservedForControlSurface(const MidiOutDevice *dev) const;
  bool isMessageAllowedWhileUsbReservedForControlSurface(
      MidiOutDevice *dev, const MidiMessage &message) const;

private:
  uint16_t activeMidiChannelMask_;
  etl::vector<MidiOutDevice *, 2> activeOutDevices_;

  etl::array<etl::vector<MidiMessage, MIDI_MAX_MESG_QUEUE>, MIDI_MAX_BUFFERS>
      queues_;

  int currentPlayQueue_;
  int currentOutQueue_;

  bool started_;
  bool sendSync_;
  UsbHostControlSurfaceType activeDetectedControlSurfaceType_;
  IControlSurface *controlSurface_;
  ApcMiniMk2Surface apcMiniMk2Surface_;
  LaunchpadMiniMk3Surface launchpadMiniMk3Surface_;
};
#endif
