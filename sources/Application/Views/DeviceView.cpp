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

#include "DeviceView.h"
#if defined(ADV)
#include "Adapters/adv/audio/tlv320aic3204.h"
#include "Adapters/adv/usb/usb_audio_handoff.h"
#endif
#include "Application/AppWindow.h"
#include "Application/Model/FirmwareVersion.h"
#include "Application/Model/Scale.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Utils/char.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/SampleEditorView.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UITempoField.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioRecording.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include "platform.h"
#include <nanoprintf.h>

#define ACTION_BOOTSEL MAKE_FOURCC('B', 'O', 'O', 'T')

static void BootselCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    System *sys = System::GetInstance();
    sys->SystemBootloader();
  }
};

DeviceView::DeviceView(GUIWindow &w, ViewData *data) : FieldView(w, data) {

  GUIPoint position = GetAnchor();

  auto config = Config::GetInstance();

  Variable *v;

  position._y += 1;
  v = config->FindVariable(FourCC::VarMidiDevice);
  intVarField_.emplace_back(position, *v, "Output Device: %s", 0, 3, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarMidiControlSurface);
  const int maxControlSurfaceValue =
      (v->GetListSize() > 0) ? v->GetListSize() - 1 : 0;
  intVarField_.emplace_back(position, *v, "Ctrl Surface: %s", 0,
                            maxControlSurfaceValue, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarMidiSync);
  // just hardcode max of 1, as only settings are "off" & "send"
  intVarField_.emplace_back(position, *v, "Transport/Sync: %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarMidiInputDevice);
  intVarField_.emplace_back(position, *v, "Input Device: %s", 0,
                            v->GetListSize() - 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarRemoteUI);
  intVarField_.emplace_back(position, *v, "Remote UI: %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarImportResampler);
  intVarField_.emplace_back(position, *v, "Import Resampler: %s", 1,
                            v->GetListSize() - 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarBacklightLevel);
  // MIN brightness is 0xF (15)
  intVarField_.emplace_back(position, *v, "Display Brightness: %2.2X", 0xF,
                            0xFF, 1, 16);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarOutputVolume);
  intVarField_.emplace_back(position, *v, "Output Volume: %3d", 0, 100, 1, 5);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarAudioOutput);
  intVarField_.emplace_back(position, *v, "Audio Output: %s", 0,
                            v->GetListSize() - 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 1;
  v = config->FindVariable(FourCC::VarInstrumentAudition);
  intVarField_.emplace_back(position, *v, "Instrument Audition: %s", 0, 1, 1,
                            1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  position._y += 2;
  actionField_.emplace_back("Theme settings", FourCC::ActionShowTheme,
                            position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._y += 2;
  actionField_.emplace_back("Update firmware", FourCC::ActionBootSelect,
                            position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  // y=17 -> y=20, just above Battery health (y=21)
  position._y += 3;
  actionField_.emplace_back("Credits", FourCC::ActionShowCredits, position);
  actionField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 17);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);
}

DeviceView::~DeviceView() {}

void DeviceView::ProcessButtonMask(unsigned short mask, bool pressed) {

  if (!pressed)
    return;

  FieldView::ProcessButtonMask(mask, pressed);

  if (mask & EPBM_EDIT) {
    if (mask & EPBM_PLAY) {
      // recording screen
      if (!Player::GetInstance()->IsRunning()) {
        switchToRecordView();
      }
    }
  } else if (mask & EPBM_NAV) {
    if (mask & EPBM_DOWN) {
      ViewType vt = VT_PROJECT;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }
  } else if (mask & EPBM_PLAY) {
    Player *player = Player::GetInstance();
    player->OnStartButton(PM_SONG, viewData_->songX_, false, viewData_->songX_);
  };
};

void DeviceView::AnimationUpdate() {
  ScreenView::AnimationUpdate();
#if defined(ADV)
  // Poll atomic USB state from the GUI task, including when playback is
  // stopped. USB callbacks must never draw or access view state directly.
  const bool inactive =
      usbMasterAudioOutputSelected() && !usbMasterAudioIsStreaming();
  if (inactive != usbStreamInactive_) {
    SetDirty(true);
  }
#endif
}

void DeviceView::DrawView() {

  Clear();

  GUITextProperties props;
  GUIPoint pos = GetTitlePosition();

  drawSectionHeaders();
  FieldView::Redraw();

  // Draw battery health
  pos._x = 1;
  pos._y = 1;
  SetColor(CD_NORMAL);
  DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y,
                   "Battery health:", props);

  pos._x += strlen("Battery health") + 1;
  int16_t soh = battery_health();
  char sohText[5];
  (soh < 0) ? npf_snprintf(sohText, sizeof(sohText), "%s", "NA")
            : npf_snprintf(sohText, sizeof(sohText), "%i%%", soh);
  SetColor(CD_NORMAL);
  DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, sohText, props);

#if defined(ADV)
  usbStreamInactive_ =
      usbMasterAudioOutputSelected() && !usbMasterAudioIsStreaming();
  if (usbStreamInactive_) {
    pos._x = SCREEN_MAP_WIDTH + 1;
    pos._y = 19;
    DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y,
                     "USB stream inactive", props);
  }
#endif

  SetColor(CD_NORMAL);
  drawMap();

  pos._x = 1;
  pos._y = 3;

  const FirmwareVersionString buildString = FormatFirmwareRelease();
  SetColor(CD_NORMAL);
  DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, buildString.c_str(),
                   props);
  pos._x += static_cast<int16_t>(buildString.size()) + 1;
  // TODO: Add board detection here; show the Advance board until then.
  DrawRegionString(TEXT_REGION_BODY_LOWER, pos._x, pos._y, "Adv", props);
};

void DeviceView::Update(Observable &, I_ObservableData *data) {
  if (!hasFocus_) {
    return;
  }

  uintptr_t fourcc = (uintptr_t)data;

  UIField *focus = GetFocus();
  focus->ClearFocus();
  focus->Draw(w_);
  w_.Flush();
  focus->SetFocus();

  // Handle brightness changes directly
  if (fourcc == FourCC::VarBacklightLevel) {
    Config *config = Config::GetInstance();
    Variable *v = config->FindVariable(FourCC::VarBacklightLevel);
    if (v) {
      unsigned char brightness = (unsigned char)v->GetInt();
      System::GetInstance()->SetDisplayBrightness(brightness);
    }
    configDirty_ = true;
  }

  Player *player = Player::GetInstance();

  switch (fourcc) {
  case FourCC::ActionBootSelect: {
    if (!player->IsRunning()) {
      MessageBox *mb = MessageBox::Create(*this, "Reboot and lose changes?",
                                          MBBF_YES | MBBF_NO);
      DoModal(mb, ModalViewCallback::create<&BootselCallback>());
    } else {
      MessageBox *mb = MessageBox::Create(*this, "Not while playing", MBBF_OK);
      DoModal(mb);
    }
    return;
  }
  case FourCC::ActionShowTheme: {
    ViewType vt = VT_THEME;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }
  case FourCC::ActionShowCredits: {
    ViewType vt = VT_CREDITS;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }
  case FourCC::VarLineOut: {
    MessageBox *mb =
        MessageBox::Create(*this, "Reboot for new Audio Level!", MBBF_OK);
    DoModal(mb);
    Config *config = Config::GetInstance();
    if (!config->Save()) {
      Trace::Error("DEVICEVIEW",
                   "Failed to save device config after line out change");
      configDirty_ = true;
    } else {
      Trace::Log("DEVICEVIEW", "Saved device config after line out change");
      configDirty_ = false;
    }
    break;
  }
  case FourCC::VarMidiDevice:
  case FourCC::VarMidiInputDevice:
  case FourCC::VarMidiControlSurface:
  case FourCC::VarMidiSync:
  case FourCC::VarRemoteUI:
  case FourCC::VarInstrumentAudition:
  case FourCC::VarImportResampler: {
    configDirty_ = true;
    break;
  }
  case FourCC::VarOutputVolume: {
    Config *config = Config::GetInstance();
    Variable *v = config->FindVariable(FourCC::VarOutputVolume);
    if (v) {
      Audio *audio = Audio::GetInstance();
      if (audio) {
        // This unfortunate name may get confused with the actual audio pipeline
        // mixer. It's not, this sets the driver output volume
        audio->SetMixerVolume(v->GetInt());
      }
    }
    configDirty_ = true;
    break;
  }
  case FourCC::VarAudioOutput: {
#if defined(ADV)
    Variable *output =
        Config::GetInstance()->FindVariable(FourCC::VarAudioOutput);
    const int mode = output != nullptr
                         ? output->GetInt()
                         : static_cast<int>(AudioOutputMode::Analog);
    const bool usbSelected = mode != static_cast<int>(AudioOutputMode::Analog);
    const bool analogEnabled = mode != static_cast<int>(AudioOutputMode::Usb);
    usbMasterAudioSetOutputSelected(usbSelected);
    tlv320_set_analog_output_enabled(analogEnabled);
#endif
    configDirty_ = true;
    break;
  }
  default:
    NInvalid;
    break;
  };
  focus->Draw(w_);
  isDirty_ = true;
};

void DeviceView::drawSectionHeaders() {
  GUITextProperties props;
  const GUIPoint anchor = GetAnchor();

  SetColor(CD_HILITE2);
  DrawRegionString(TEXT_REGION_BODY, anchor._x, anchor._y, "MIDI", props);
}

void DeviceView::addSwatchField(ColorDefinition color, GUIPoint position) {
  position._x -= 5;
  swatchField_.emplace_back(position, color);
  fieldList_.insert(fieldList_.end(), &(*swatchField_.rbegin()));
}

void DeviceView::OnFocus() { setTitle(etl::string<23>("Device")); }

void DeviceView::OnFocusLost() {
  if (IsSavingRecording()) {
    Trace::Log("DEVICEVIEW",
               "Skipping device config save while recording is being saved");
    return;
  }

  if (configDirty_) {
    Config *config = Config::GetInstance();
    if (!config->Save()) {
      Trace::Error("DEVICEVIEW", "Failed to save device config on focus lost");
      return;
    }
    Trace::Log("DEVICEVIEW", "Saved device config on focus lost");
    configDirty_ = false;
  }
}
