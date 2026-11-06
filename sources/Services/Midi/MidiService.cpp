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

#include "MidiService.h"
#include "Application/Model/Config.h"
#include "Application/Model/Project.h"
#include "Application/Player/Player.h"
#include "Application/Player/SyncMaster.h"
#include "Services/Audio/AudioDriver.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "System/Timer/Timer.h"
#include "config/MemorySections.h"
#include <cstdint>

namespace {
constexpr int kControlSurfaceOff = 0;
constexpr int kControlSurfaceAuto = 1;
} // namespace

MidiService::MidiService()
    : usbControlSurfaceOutDevice_(nullptr), activeMidiChannelMask_(0),
      started_(false), sendSync_(true),
      activeDetectedControlSurfaceType_(USB_HOST_CONTROL_SURFACE_NONE),
      controlSurface_(nullptr) {
  for (int i = 0; i < MIDI_MAX_BUFFERS; i++) {
    queues_[i].clear();
  }
  sendSync_ = Config::GetInstance()->GetValue("MIDISYNC") > 0;
};

MidiService::~MidiService() { Close(); };

bool MidiService::Init() {
  outList_.empty();
  inList_.empty();
  // Init all the output midi devices
  for (auto dev : outList_) {
    dev->Init();
  }

  for (auto dev : inList_) {
    // Transport is handled directly by MidiInDevice. MidiService must not
    // observe the input's differently typed message payloads.
    dev->RemoveObserver(*this);
    dev->Start();
  }

  // MIDI input devices start on the first program and load the project's
  // defaults after project data has been restored.

  auto config = Config::GetInstance();
  auto midiDevVar =
      (WatchedVariable *)config->FindVariable(FourCC::VarMidiDevice);
  midiDevVar->AddObserver(*this);

  auto activeDeviceConfig = midiDevVar->GetInt();
  updateActiveDevicesList(activeDeviceConfig);

  auto midiInputDevVar =
      (WatchedVariable *)config->FindVariable(FourCC::VarMidiInputDevice);
  midiInputDevVar->AddObserver(*this);
  updateActiveInputDevicesList(midiInputDevVar->GetInt());

  auto midiSyncVar =
      (WatchedVariable *)config->FindVariable(FourCC::VarMidiSync);
  midiSyncVar->AddObserver(*this);
  auto sync = midiSyncVar->GetInt();
  sendSync_ = sync != 0;

  auto midiControlSurfaceVar =
      (WatchedVariable *)config->FindVariable(FourCC::VarMidiControlSurface);
  midiControlSurfaceVar->AddObserver(*this);
  reconcileControlSurfaceSelection();

  return true;
};

void MidiService::Close() { Stop(); };

bool MidiService::Start() {
  currentPlayQueue_ = 0;
  currentOutQueue_ = 0;
  started_ = true;
  startDevice();
  if (controlSurface_ != nullptr) {
    controlSurface_->Start();
  }
  return true;
};

void MidiService::Stop() {
  started_ = false;
  if (controlSurface_ != nullptr) {
    controlSurface_->Stop();
  }
  stopDevice();
};

bool MidiService::HasControlSurfaceSongWindow() const {
  return (controlSurface_ != nullptr) && controlSurface_->HasSongWindow();
}

uint32_t MidiService::GetControlSurfaceSongWindowOffset() const {
  if (controlSurface_ == nullptr) {
    return 0;
  }
  return controlSurface_->GetSongWindowOffset();
}

uint32_t MidiService::GetControlSurfaceSongWindowRowCount() const {
  if (controlSurface_ == nullptr) {
    return 0;
  }
  return controlSurface_->GetSongWindowRowCount();
}

void MidiService::QueueMessage(MidiMessage &m) {
  if (!activeOutDevices_.empty()) {
    auto queue = &queues_[currentPlayQueue_];
    queue->emplace_back(m.status_, m.data1_, m.data2_);
  }
};

void MidiService::SendImmediate(MidiMessage &m) {
  if (activeOutDevices_.empty()) {
    return;
  }

  for (auto dev : activeOutDevices_) {
    if (!isMessageAllowedWhileUsbReservedForControlSurface(dev, m)) {
      continue;
    }
    dev->SendMessage(m);
  }
}

void MidiService::SendRawImmediate(const uint8_t *data, uint8_t len) {
  if (activeOutDevices_.empty()) {
    return;
  }

  for (auto dev : activeOutDevices_) {
    if (isUsbOutReservedForControlSurface(dev)) {
      continue;
    }
    dev->SendRawBytes(data, len);
  }
}

void MidiService::SendControlSurfaceImmediate(MidiMessage &m) {
  if (usbControlSurfaceOutDevice_ != nullptr) {
    usbControlSurfaceOutDevice_->SendMessage(m);
  }
}

void MidiService::SendControlSurfaceRawImmediate(const uint8_t *data,
                                                 uint8_t len) {
  if (usbControlSurfaceOutDevice_ != nullptr) {
    usbControlSurfaceOutDevice_->SendRawBytes(data, len);
  }
}

PICOTRACKER_FAST_CODE("midi_service_trigger")
void MidiService::Trigger() {
  AdvancePlayQueue();

  if (!activeOutDevices_.empty() && sendSync_) {
    SyncMaster *sm = SyncMaster::GetInstance();
    if (sm->MidiSlice()) {
      MidiMessage msg;
      msg.status_ = 0xF8;
      QueueMessage(msg);
    }
  }
}

PICOTRACKER_FAST_CODE("midi_service_advance_play_queue")
void MidiService::AdvancePlayQueue() {
  currentPlayQueue_ = (currentPlayQueue_ + 1) % MIDI_MAX_BUFFERS;
  auto queue = &queues_[currentPlayQueue_];
  queue->clear();
}

void MidiService::Update(Observable &o, I_ObservableData *d) {
  AudioDriver::Event *event = (AudioDriver::Event *)d;
  if (event->type_ == AudioDriver::Event::ADET_DRIVERTICK) {
    onAudioTick();
  }
  WatchedVariable &v = (WatchedVariable &)o;
  switch (v.GetID()) {
    // need braces inside case statements due to:
    // https://stackoverflow.com/a/11578973/85472
  case FourCC::VarMidiDevice: {
    auto activeDeviceConfig = v.GetInt();
    // note deviceID has 0 == OFF
    Trace::Debug("midi device var changed:%d", activeDeviceConfig);

    stopDevice();
    updateActiveDevicesList(activeDeviceConfig);
    startDevice();
  } break;
  case FourCC::VarMidiInputDevice: {
    auto activeInputConfig = v.GetInt();
    Trace::Debug("midi input device var changed:%d", activeInputConfig);
    updateActiveInputDevicesList(activeInputConfig);
  } break;
  case FourCC::VarMidiSync: {
    auto sync = v.GetInt();
    sendSync_ = sync != 0;
  } break;
  case FourCC::VarMidiControlSurface: {
    activeDetectedControlSurfaceType_ = USB_HOST_CONTROL_SURFACE_NONE;
    reconcileControlSurfaceSelection();
  } break;
  }
}

void MidiService::onAudioTick() { flushOutQueue(); }

void MidiService::Flush() { flushOutQueue(); };

void MidiService::OnMidiStart() {
  // Start the Player in song mode
  Trace::Log("MIDI", "Received MIDI Start message");
  Player::GetInstance()->Start(PM_SONG, true, MSM_AUDIO, false);
}

void MidiService::OnMidiStop() {
  // Stop the Player
  Trace::Log("MIDI", "Received MIDI Stop message");
  Player::GetInstance()->Stop();
}

void MidiService::OnMidiClock() {
  // Handle MIDI clock messages if needed
  // This could be used for tempo synchronization
}

void MidiService::RefreshControlSurfaceSelection() {
  activeDetectedControlSurfaceType_ = USB_HOST_CONTROL_SURFACE_NONE;
  reconcileControlSurfaceSelection();
}

void MidiService::ReloadControlSurface() {
  if (controlSurface_ == nullptr) {
    return;
  }

  controlSurface_->Stop();
  controlSurface_->Init(*this);
  registerControlSurfaceInputs(*controlSurface_);
  controlSurface_->SetEnabled(true);
  if (started_) {
    controlSurface_->Start();
  }
}

void MidiService::LoadProjectSettings(const Project &project) {
  for (int midiChannel = 0; midiChannel < MIDI_INPUT_CHANNEL_COUNT;
       midiChannel++) {
    SetInputProgramForChannel(
        midiChannel, project.song_.GetMidiDefaultInstrumentIndex(midiChannel));
  }
}

void MidiService::SetInputProgramForChannel(int midiChannel, uint8_t program) {
  for (MidiInDevice *device : inList_) {
    if (device != nullptr) {
      device->SetProgramForChannel(midiChannel, program);
    }
  }
}

bool MidiService::IsInputProgramSelected(int midiChannel,
                                         uint8_t program) const {
  for (const MidiInDevice *device : activeInDevices_) {
    if (device != nullptr &&
        device->GetProgramForChannel(midiChannel) == program) {
      return true;
    }
  }
  return false;
}

void MidiService::flushOutQueue() {
  auto flushQueue = &queues_[currentOutQueue_];

  for (auto dev : activeOutDevices_) {
    if (!isUsbOutReservedForControlSurface(dev)) {
      dev->SendQueue(*flushQueue);
      continue;
    }

    for (auto &msg : *flushQueue) {
      if (isMessageAllowedWhileUsbReservedForControlSurface(dev, msg)) {
        dev->SendMessage(msg);
      }
    }
  }
  flushQueue->clear();

  // Move queue positions after flush
  currentOutQueue_ = (currentOutQueue_ + 1) % MIDI_MAX_BUFFERS;
}

void MidiService::updateActiveDevicesList(unsigned short config) {
  activeOutDevices_.clear();

  switch (config) {
  case 1:
    activeOutDevices_.insert(activeOutDevices_.end(), outList_[0]);
    break;
  case 2:
    activeOutDevices_.insert(activeOutDevices_.end(), outList_[1]);
    break;
  case 3:
    activeOutDevices_.insert(activeOutDevices_.end(), outList_[0]);
    activeOutDevices_.insert(activeOutDevices_.end(), outList_[1]);
    break;
  }
}

void MidiService::updateActiveInputDevicesList(unsigned short config) {
  activeInDevices_.clear();
  for (size_t deviceIndex = 0; deviceIndex < inList_.size(); deviceIndex++) {
    MidiInDevice *device = inList_[deviceIndex];
    // Keep this service detached if input selection is reconfigured.
    device->RemoveObserver(*this);
    const bool shouldBeActive =
        (config & (1U << static_cast<unsigned int>(deviceIndex))) != 0U;
    if (shouldBeActive) {
      activeInDevices_.insert(activeInDevices_.end(), device);
      if (!device->IsRunning()) {
        device->Start();
      }
    } else {
      if (device->IsRunning()) {
        device->Stop();
      }
    }
  }
}

void MidiService::registerUsbControlSurfaceOutDevice(MidiOutDevice &device) {
  usbControlSurfaceOutDevice_ = &device;
}

void MidiService::configureControlSurface(int surfaceSelection) {
  IControlSurface *selectedSurface = nullptr;
  switch (surfaceSelection) {
  case kControlSurfaceAuto:
    switch (activeDetectedControlSurfaceType_) {
    case USB_HOST_CONTROL_SURFACE_LAUNCHPAD_MINI_MK3:
      selectedSurface = &launchpadMiniMk3Surface_;
      break;
    case USB_HOST_CONTROL_SURFACE_APC_MINI_MK2:
      selectedSurface = &apcMiniMk2Surface_;
      break;
    default:
      break;
    }
    break;
  case kControlSurfaceOff:
  default:
    break;
  }

  if (controlSurface_ == selectedSurface) {
    return;
  }

  if (controlSurface_ != nullptr) {
    controlSurface_->Stop();
    controlSurface_->SetEnabled(false);
  }

  controlSurface_ = selectedSurface;
  if (controlSurface_ == nullptr) {
    return;
  }

  controlSurface_->Init(*this);
  registerControlSurfaceInputs(*controlSurface_);
  controlSurface_->SetEnabled(true);
  if (started_) {
    controlSurface_->Start();
  }
}

void MidiService::reconcileControlSurfaceSelection() {
  auto config = Config::GetInstance();
  if (config == nullptr) {
    return;
  }

  auto *midiControlSurfaceVar = static_cast<WatchedVariable *>(
      config->FindVariable(FourCC::VarMidiControlSurface));
  if (midiControlSurfaceVar == nullptr) {
    return;
  }

  const int surfaceSelection = midiControlSurfaceVar->GetInt();
  UsbHostControlSurfaceType detectedSurfaceType = USB_HOST_CONTROL_SURFACE_NONE;
  if (surfaceSelection == kControlSurfaceAuto) {
    detectedSurfaceType = usbHostControlSurfaceType();
  }

  activeDetectedControlSurfaceType_ = detectedSurfaceType;
  configureControlSurface(surfaceSelection);
}

void MidiService::registerControlSurfaceInputs(IControlSurface &surface) {
  for (auto dev : inList_) {
    if (dev != nullptr) {
      surface.RegisterMidiInput(*dev);
    }
  }
}

void MidiService::startDevice() {
  // look for the device
  for (auto dev : activeOutDevices_) {
    dev->Start();
  }
};

void MidiService::stopDevice() {
  for (auto dev : outList_) {
    dev->Stop();
  }
};

void MidiService::OnPlayerStart() {
  // Stop and restart devices to ensure clean state
  stopDevice();
  startDevice();

  // Send MIDI Start message (0xFA) to all active output devices
  if (sendSync_) {
    MidiMessage msg;
    msg.status_ = MidiMessage::MIDI_START;
    QueueMessage(msg);
  }
};

void MidiService::RegisterActiveChannel(uint8_t channel) {
  activeMidiChannelMask_ |= static_cast<uint16_t>(1u << (channel & 0x0F));
}

bool MidiService::isUsbOutReservedForControlSurface(
    const MidiOutDevice *dev) const {
  return (usbControlSurfaceOutDevice_ != nullptr) &&
         (controlSurface_ != nullptr) && (dev == usbControlSurfaceOutDevice_);
}

bool MidiService::isMessageAllowedWhileUsbReservedForControlSurface(
    MidiOutDevice *dev, const MidiMessage &message) const {
  if (!isUsbOutReservedForControlSurface(dev)) {
    return true;
  }

  switch (message.status_) {
  case MidiMessage::MIDI_CLOCK:
  case MidiMessage::MIDI_START:
  case MidiMessage::MIDI_CONTINUE:
  case MidiMessage::MIDI_STOP:
  case MidiMessage::MIDI_ACTIVE_SENSING:
  case MidiMessage::MIDI_SYSTEM_RESET:
    return true;
  default:
    return false;
  }
}

void MidiService::OnPlayerStop() {
  for (uint8_t channel = 0; channel < 16; channel++) {
    if ((activeMidiChannelMask_ & (static_cast<uint16_t>(1u << channel))) ==
        0) {
      continue;
    }
    MidiMessage ccMsg;
    ccMsg.status_ = MidiMessage::MIDI_CONTROL_CHANGE + channel;
    ccMsg.data1_ = MidiCC::CC_ALL_NOTES_OFF;
    ccMsg.data2_ = 0x00;
    QueueMessage(ccMsg);
  }
  activeMidiChannelMask_ = 0;
  // Send MIDI Stop message (0xFC) to all active output devices
  if (sendSync_) {
    MidiMessage msg;
    msg.status_ = MidiMessage::MIDI_STOP;
    QueueMessage(msg);
  }
};
