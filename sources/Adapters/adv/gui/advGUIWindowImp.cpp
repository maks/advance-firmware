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

#include "advGUIWindowImp.h"
#include "Adapters/adv/audio/record.h"
#include "Adapters/adv/filesystem/advFileSystem.h"
#include "Adapters/adv/system/advUsbRoleManager.h"
#include "Adapters/adv/system/charger.h"
#include "Application/Model/Config.h"
#include "Application/Utils/char.h"
#include "Player/Player.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "advRemoteUI.h"
#include <string.h>
#include <string>

// Keep track of the last "SetColor()" call to track the current color palette
// index, used by DrawRect() to know which color to use when drawing to the
// devices LCD
static uint8_t lastRemoteColorIdx = 255;

void appwindow_set_sdcard_present(bool present);

advGUIWindowImp *instance_;

advGUIWindowImp::advGUIWindowImp(GUICreateWindowParams &p) {

  // stash ref to singleton for use in static methods
  instance_ = this;

  Config *config = Config::GetInstance();

  auto remoteUIVar =
      (WatchedVariable *)config->FindVariable(FourCC::VarRemoteUI);

  // register to receive updates to remoteui setting
  remoteUIVar->AddObserver(*this);
  auto remoteui = remoteUIVar->GetInt();
  remoteUIEnabled_ = remoteui != 0;
  display_set_overlay_observer(remoteUIEnabled_ ? ObserveOverlay : nullptr);
};

advGUIWindowImp::~advGUIWindowImp() {}

void advGUIWindowImp::DrawChar(const char c, const GUIPoint &pos,
                               const GUITextProperties &p) {
  //  Trace::Debug("Draw char \"%c\" at pos x:%ld (%ld), y:%ld (%ld) - invert:
  //  %d", c, pos._x, pos._x, pos._y, pos._y, p.invert_);

  int32_t x = pos._x;
  int32_t y = pos._y;
  if (x < 0 || y < 0 || x > 255 || y > 255)
    return;
  uint8_t target = display_target();
  if (target) {
    if (x >= 32 || y >= 24)
      return;
    display_draw_overlay_char(x, y, c, p.invert_);
  } else {
    display_putc_region(p.region_, x, y, c, p.invert_);
  }
  // Send native glyph identity and clipping, including hidden updates. The
  // client has the fonts and routes covered drawing into its own live backing.
  if (!remoteUIEnabled_)
    return;
  GraphicsRegion bounds;
  uint8_t font = TEXT_FONT_NORMAL;
  if (target) {
    bounds = {int16_t(8 + x * 22), int16_t(y * 30), 22, 30};
  } else {
    int32_t column = x, row = y;
    uint8_t id = p.region_;
    if (id >= display_text_layout()->count || column < 0 || row < 0)
      return;
    const TextRegionInfo &r = display_text_layout()->regions[id];
    if (column >= r.columns || row >= r.rows)
      return;
    font = r.definition.font;
    bounds = {int16_t(r.definition.x + column * r.glyph_width),
              int16_t(r.definition.y + row * r.glyph_height), r.glyph_width,
              r.glyph_height};
  }
  uint8_t character = static_cast<uint8_t>(c);
  if (character < 32)
    return;
  if (character - 32 >= text_font(font)->count)
    character = '?';
  struct GlyphCommand {
    advGUIWindowImp *window;
    GraphicsRegion bounds;
    uint8_t target, font, character;
    bool invert;
  } command{this, bounds, target, font, character, p.invert_};
  display_visit_pixels(
      bounds, true,
      [](void *ctx, GraphicsRegion clip, uint16_t *, uint16_t, bool) {
        auto &g = *static_cast<GlyphCommand *>(ctx);
        g.window->SendGlyph(g.target, g.font, g.character, g.invert, g.bounds,
                            clip);
      },
      &command);
}

void advGUIWindowImp::DrawString(const char *string, const GUIPoint &pos,
                                 const GUITextProperties &p, bool overlay) {
  Trace::Debug("draw string");
  if (!string)
    return;
  GUIPoint cell(pos);
  while (*string) {
    DrawChar(*string++, cell, p);
    ++cell._x;
  }
};

void advGUIWindowImp::DrawRect(GUIRect &r) {
  // This is the local drawing command for the device's own screen.
  display_fill_rect(lastRemoteColorIdx, r.Left(), r.Top(), r.Width(),
                    r.Height());
  if (remoteUIEnabled_) {
    display_visit_pixels(
        {int16_t(r.Left()), int16_t(r.Top()), int16_t(r.Width()),
         int16_t(r.Height())},
        false,
        [](void *context, GraphicsRegion bounds, uint16_t *, uint16_t, bool) {
          static_cast<advGUIWindowImp *>(context)->SendRect(bounds);
        },
        this);
  }
}

void advGUIWindowImp::ObserveOverlay(DisplayOverlayEvent event,
                                     GraphicsRegion bounds) {
  RemoteUICommand command =
      event == DISPLAY_OVERLAY_PUSH
          ? OVERLAY_PUSH_CMD
          : (event == DISPLAY_OVERLAY_POP ? OVERLAY_POP_CMD
                                          : OVERLAY_RESET_CMD);
  char buffer[18];
  uint16_t size = remoteUIOverlayCommand(command, bounds, buffer);
  instance_->sendToUSBCDCBuffered(buffer, size);
}

void advGUIWindowImp::SendRect(GraphicsRegion r) {
  // Full escaping: marker, command, target and four two-byte coordinates.
  char buffer[20];
  uint16_t size = remoteUITargetRectCommand(display_target(), r, buffer);
  sendToUSBCDCBuffered(buffer, size);
}

void advGUIWindowImp::SendGlyph(uint8_t target, uint8_t font, uint8_t c,
                                bool invert, GraphicsRegion glyph,
                                GraphicsRegion clip) {
  char buffer[34];
  const bool clipped = clip.x != glyph.x || clip.y != glyph.y ||
                       clip.width != glyph.width || clip.height != glyph.height;
  uint16_t size = remoteUIGlyphCommand(target, font, c, invert, glyph.x,
                                       glyph.y, clip, clipped, buffer);
  sendToUSBCDCBuffered(buffer, size);
}

void advGUIWindowImp::Clear(GUIColor &c, bool overlay) {
  color_t backgroundColor = GetColor(c);
  display_set_background(backgroundColor);
  display_clear(backgroundColor);
  if (remoteUIEnabled_) {
    char remoteUIBuffer[5];
    remoteUIClearCommand(c._r, c._g, c._b, remoteUIBuffer);
    sendToUSBCDCBuffered(remoteUIBuffer, 5); // Use the buffered function
  }
};

void advGUIWindowImp::ClearTextRect(GUIRect &r) {
  Trace::Debug("GUI ClearTextRect call");
};

color_t advGUIWindowImp::GetColor(GUIColor &c) {
  // Palette index should always be < 16. Wont check it.
  // TODO: should not be redefining the palette colors every call
  display_set_palette_color(c._paletteIndex, to_rgb565(c));
  return (color_t)c._paletteIndex;
}

void advGUIWindowImp::SetColor(GUIColor &c) {
  color_t color = GetColor(c);
  lastRemoteColorIdx = color;
  display_set_foreground(color);
  if (remoteUIEnabled_) {
    // Buffer must be large enough for the worst case where all 3 color bytes
    // are escaped. Header (2) + 3 color components * 2 bytes/escaped_component
    // = 8 bytes.
    char remoteUIBuffer[8];
    auto bufferIndex =
        remoteUISetColorCommand(c._r, c._g, c._b, remoteUIBuffer);
    sendToUSBCDCBuffered(remoteUIBuffer, bufferIndex);
  }
};

void advGUIWindowImp::Lock(){};

void advGUIWindowImp::Unlock(){};

void advGUIWindowImp::Flush() {
  display_draw_changed();
  // send buffered UI draw cmds out over USB
  if (remoteUIEnabled_) {
    flushRemoteUIBuffer();
  }
};

void advGUIWindowImp::Invalidate() {
  Event ev(FLUSH);
  xQueueSend(eventQueue, &ev, 0);
};

void advGUIWindowImp::PushEvent(GUIEvent &event) {
  Trace::Debug("GUI PushEvent");
};

GUIRect advGUIWindowImp::GetRect() {
  Trace::Debug("GUI GetRect");
  return GUIRect(0, 0, 320, 240);
}

void advGUIWindowImp::ProcessEvent(Event &event) {
  switch (event.type_) {
  case REDRAW:
    instance_->_window->Update(true);
    break;
  case FLUSH:
    instance_->_window->Update(false);
    break;
  case CLOCK:
    instance_->_window->ClockTick();
    break;
  case BUTTON_CHANGE:
    ProcessButtonChange(event.buttonChangeMask_, event.buttonMask_);
    break;
  case SD_DET_INSERT: {
    Trace::Log("SDCARD", "Card inserted, reinitializing");
    FATFS_UnLinkDriver(SDPath);
    HAL_SD_DeInit(&hsd1);
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
    MX_SDMMC1_SD_Init();
    FATFS_LinkDriver(&SD_DMA_Driver, SDPath);
    bool mounted =
        (f_mount(&SDFatFS, (TCHAR const *)SDPath, 0) == FR_OK) ? true : false;
    if (mounted) {
      auto fs = FileSystem::GetInstance();
      if (!fs || !fs->exists("/")) {
        mounted = false;
      }
    }
    if (!mounted) {
      Trace::Error("Failed to mount after insert");
    }
    appwindow_set_sdcard_present(mounted);
  } break;
  case SD_DET_REMOVE: {
    Trace::Log("SDCARD", "Card removed, pausing");
    if (IsRecordingActive()) {
      StopRecording();
    }
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    FATFS_UnLinkDriver(SDPath);
    HAL_SD_DeInit(&hsd1);
    appwindow_set_sdcard_present(false);
  } break;
  case CHARGER_INT_EVENT: {
    const ChargerInt reason = chargerIntReason();
    switch (reason) {
    case VBUS_OK:
      startCharging();
      break;
    case VBUS_KO:
    case CHARGE_END:
    case CHARGE_FAULT:
      stopCharging();
      break;
    case UNKNOWN:
    default:
      break;
    }
  } break;
  case USB_ROLE_INT_EVENT:
    advUsbRoleManagerService();
    break;
  case LAST:
    break;
  }
}

void advGUIWindowImp::ProcessButtonChange(uint16_t changeMask,
                                          uint16_t buttonMask) {
  int e = 1;
  System *system = System::GetInstance();
  unsigned long now = system->GetClock();
  for (uint32_t i = 0; i < sizeof(eventMappingPico); i++) {
    if (changeMask & e) {
      GUIEventType type = (buttonMask & e) ? ET_PADBUTTONDOWN : ET_PADBUTTONUP;

      GUIEvent event(eventMappingPico[i], type, now, 0, 0, 0);
      instance_->_window->DispatchEvent(event);
    }
    e = e << 1;
  }
}

void advGUIWindowImp::Update(Observable &o, I_ObservableData *d) {
  WatchedVariable &v = (WatchedVariable &)o;
  switch (v.GetID()) {
  case FourCC::VarRemoteUI: {
    auto remoteui = v.GetInt();
    remoteUIEnabled_ = remoteui != 0;
    display_set_overlay_observer(remoteUIEnabled_ ? ObserveOverlay : nullptr);
    if (remoteUIEnabled_) {
      Event event(REDRAW);
      xQueueSend(eventQueue, &event, 0);
    }
  } break;
  }
}

// Buffer draw commands to be sent to remote UI over USB
void advGUIWindowImp::sendToUSBCDCBuffered(const char *buf, uint32_t len) {
  if (!remoteUIEnabled_ || len == 0)
    return;

  // If the new data won't fit, flush the buffer first to make space.
  if ((remoteUIBufferPos_ + len) > sizeof(remoteUIDrawBuffer_)) {
    flushRemoteUIBuffer();
  }

  // Copy data into the buffer
  memcpy(remoteUIDrawBuffer_ + remoteUIBufferPos_, buf, len);
  remoteUIBufferPos_ += len;
}

void advGUIWindowImp::flushRemoteUIBuffer() {
  if (remoteUIBufferPos_ > 0) {
    // This is the function you were already using, which calls tud_cdc_write,
    // etc.
    sendToUSBCDC(remoteUIDrawBuffer_, remoteUIBufferPos_);
    remoteUIBufferPos_ = 0; // Reset the buffer position
  }
}
