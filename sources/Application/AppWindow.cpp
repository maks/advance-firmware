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

#include "AppWindow.h"
#include "Application/Commands/ApplicationCommandDispatcher.h"
#include "Application/Commands/EventDispatcher.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/FirmwareVersion.h"
#include "Application/Model/Mixer.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/char.h"
#include "Application/Views/ChainView.h"
#include "Application/Views/ConsoleView.h"
#include "Application/Views/DeviceView.h"
#include "Application/Views/FM6PresetBrowserView.h"
#include "Application/Views/GrooveView.h"
#include "Application/Views/ImportView.h"
#include "Application/Views/InstrumentImportView.h"
#include "Application/Views/InstrumentView.h"
#include "Services/Audio/AudioRecording.h"

#include "Adapters/adv/display/display.h"
#include "Adapters/adv/system/assets/error_bitmaps.h"
#include "Adapters/adv/system/critical_error_message.h"
#include "Application/Views/CreditsView.h"
#include "Application/Views/MasterEffectsView.h"
#include "Application/Views/MixerView.h"
#include "Application/Views/ModalDialogs/FullScreenBox.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/NullView.h"
#include "Application/Views/PhraseView.h"
#include "Application/Views/ProjectView.h"
#include "Application/Views/RecordView.h"
#include "Application/Views/SampleEditorView.h"
#include "Application/Views/SampleSlicesView.h"
#include "Application/Views/SelectProjectView.h"
#include "Application/Views/SongView.h"
#include "Application/Views/TableView.h"
#include "Application/Views/ThemeImportView.h"
#include "Application/Views/ThemeView.h"
#include "Application/Views/VoiceView.h"
#include "BaseClasses/View.h"
#include "Externals/etl/include/etl/string.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Player/Player.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/Display/ErrorBitmap.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Profiler/DWTProfiler.h"
#include "System/System/System.h"
#include "UIFramework/BasicDatas/TextHighlight.h"
#include "UIFramework/Interfaces/I_GUIWindowFactory.h"
#include "Views/UIController.h"
#include "platform.h"
#include <nanoprintf.h>
#include <string.h>

namespace {
constexpr uint16_t kModifierButtonMask =
    EPBM_ALT | EPBM_EDIT | EPBM_ENTER | EPBM_NAV | EPBM_SELECT;
constexpr uint16_t kDirectionalButtonMask =
    EPBM_LEFT | EPBM_DOWN | EPBM_RIGHT | EPBM_UP;

uint16_t BuildButtonEventMask(uint16_t heldMask, uint16_t changedMask,
                              bool pressed) {
  const uint16_t heldModifiers = heldMask & kModifierButtonMask;
  const uint16_t heldNonDirectionalMask = heldMask & ~kDirectionalButtonMask;
  if (!pressed) {
    return heldNonDirectionalMask;
  }

  if ((changedMask & kModifierButtonMask) != 0) {
    return heldNonDirectionalMask;
  }

  return heldModifiers | changedMask;
}
} // namespace

const uint16_t AUTOSAVE_INTERVAL_IN_SECONDS = 1 * 60;

#define MINIMUM_ALLOWED_BATTERY_PERCENTAGE 2

#define MIN_BATT_POWEROFF_SEC 15

namespace {

constexpr uint16_t kProfileOrderGraphicsBackend = 70;
constexpr uint16_t kProfileOrderGraphicsTotal = 71;

DWTProfileStats gGraphicsBackendProfile = {
    .name_ = "gfx draw",
    .printLast_ = true,
};

DWTProfileStats gGraphicsFlushProfile = {
    .name_ = "gfx total",
    .printLast_ = true,
};

void registerAppWindowProfiles() {
  static bool registered = false;
  if (registered) {
    return;
  }

  gGraphicsBackendProfile.displayOrder_ = kProfileOrderGraphicsBackend;
  RegisterDWTProfileStats(&gGraphicsBackendProfile);
  gGraphicsFlushProfile.displayOrder_ = kProfileOrderGraphicsTotal;
  RegisterDWTProfileStats(&gGraphicsFlushProfile);
  registered = true;
}

void drawFullscreenBitmap(const ErrorBitmap &bitmap) {
  display_draw_mono_bitmap_rgb565(0, 0, bitmap.data, bitmap.width,
                                  bitmap.height, bitmap.rowBytes, 0x0000,
                                  0xFFFF);
}

} // namespace

AppWindow *instance = 0;

AppWindow::OverlayCache AppWindow::overlays_[GRAPHICS_OVERLAY_LIMIT];

unsigned char AppWindow::_charScreen[TEXT_REGION_CAPACITY] TEXT_STATE_RAM;
unsigned char AppWindow::_charScreenProp[TEXT_REGION_CAPACITY] TEXT_STATE_RAM;
unsigned char AppWindow::_preScreen[TEXT_REGION_CAPACITY] TEXT_STATE_RAM;
unsigned char AppWindow::_preScreenProp[TEXT_REGION_CAPACITY] TEXT_STATE_RAM;

GUIColor AppWindow::backgroundColor_(0x0F, 0x0F, 0x0F, 0);
GUIColor AppWindow::normalColor_(0xAD, 0xAD, 0xAD, 1);
GUIColor AppWindow::highlightColor_(0x84, 0x6F, 0x94, 2);
GUIColor AppWindow::highlight2Color_(0x6B, 0x31, 0x6B, 3);
GUIColor AppWindow::cursorColor_(0x77, 0x6B, 0x56, 4);
GUIColor AppWindow::consoleColor_(0xFF, 0x00, 0xFF, 5);
GUIColor AppWindow::infoColor_(0x29, 0xEE, 0x3D, 6);
GUIColor AppWindow::warnColor_(0xEF, 0xFA, 0x52, 7);
GUIColor AppWindow::errorColor_(0xE8, 0x4D, 0x15, 8);
GUIColor AppWindow::accentColor_(0x02, 0xFF, 0x02, 9);
GUIColor AppWindow::accentAltColor_(0xFF, 0x02, 0x02, 10);
GUIColor AppWindow::emphasisColor_(0xFF, 0xA5, 0x02, 11);
GUIColor AppWindow::reserved1Color_(0x02, 0x02, 0xFF, 12);
GUIColor AppWindow::reserved2Color_(0x55, 0x55, 0x55, 13);
GUIColor AppWindow::reserved3Color_(0x77, 0x77, 0x77, 14);
GUIColor AppWindow::reserved4Color_(0xFF, 0xFF, 0x00, 15);

// Initialize the animation frame counter
uint32_t AppWindow::animationFrameCounter_ = 0;

struct AppWindowViews {
  SongView songView;
  ChainView chainView;
  PhraseView phraseView;
  DeviceView deviceView;
  ThemeView themeView;
  ThemeImportView themeImportView;
  ProjectView projectView;
  ImportView importView;
  InstrumentImportView instrumentImportView;
  FM6PresetBrowserView fm6PresetBrowserView;
  InstrumentView instrumentView;
  VoiceView voiceView;

  TableView tableView;
  GrooveView grooveView;
  SelectProjectView selectProjectView;
  MixerView mixerView;
  MasterEffectsView masterEffectsView;
  SampleEditorView sampleEditorView;
  SampleSlicesView sampleSlicesView;
  RecordView recordView;
  NullView nullView;
  CreditsView creditsView;

  AppWindowViews(GUIWindow &w, ViewData &viewData)
      : songView(w, &viewData), chainView(w, &viewData),
        phraseView(w, &viewData), deviceView(w, &viewData),
        themeView(w, &viewData), themeImportView(w, &viewData),
        projectView(w, &viewData), importView(w, &viewData),
        instrumentImportView(w, &viewData), fm6PresetBrowserView(w, &viewData),
        instrumentView(w, &viewData), voiceView(w, &viewData),
        tableView(w, &viewData), grooveView(w, &viewData),
        selectProjectView(w, &viewData), mixerView(w, &viewData),
        masterEffectsView(w, &viewData), sampleEditorView(w, &viewData),
        sampleSlicesView(w, &viewData), recordView(w, &viewData),
        nullView(w, &viewData), creditsView(w, &viewData) {}
};

void AppWindow::defineColor(FourCC colorCode, GUIColor &color,
                            int paletteIndex) {

  Config *config = Config::GetInstance();
  auto rgbVar = config->FindVariable(colorCode);
  if (rgbVar) {
    const int32_t rgbValue = rgbVar->GetInt();
    unsigned short r, g, b;
    r = (rgbValue >> 16) & 0xFF;
    g = (rgbValue >> 8) & 0xFF;
    b = rgbValue & 0xFF;
    // Always preserve the palette index when updating colors
    color = GUIColor(r, g, b, paletteIndex);
  } else {
    // Even if we don't update the RGB values, ensure the palette index is
    // correct
    color._paletteIndex = paletteIndex;
  }
}

AppWindow::AppWindow(I_GUIWindowImp &imp, const char *projectName)
    : GUIWindow(imp), originalProjectName_(projectName), project_(projectName),
      viewData_(&project_), views_(nullptr), _currentView(nullptr) {
  registerAppWindowProfiles();

  instance = this;

  // Init all members

  _statusLine[0] = 0;

  _currentView = nullptr;
  _closeProject = false;
  _lastA = 0;
  _lastB = 0;
  _mask = 0;
  lowBatteryMessageShown_ = false;
  lowBatteryWarningCounter_ = 0;
  sdCardMissing_ = false;
  sdCardMessageShown_ = false;

  npf_snprintf(projectName_, sizeof(projectName_), "%s", projectName);

  EventDispatcher *ed = EventDispatcher::GetInstance();
  ed->SetWindow(this);

  Status::Install(this);

  // Init midi services
  MidiService::GetInstance()->Init();

  UpdateColorsFromConfig();

  const TextLayout startupLayout = NullView::Layout();
  ConfigureTextRegions(startupLayout.regions, startupLayout.count);

  static AppWindowViews views(*this, viewData_);
  views_ = &views;

  _currentView = &views_->nullView;
  views_->nullView.SetDirty(true);

  views_->songView.AddObserver(*this);
  views_->chainView.AddObserver(*this);
  views_->phraseView.AddObserver(*this);
  views_->deviceView.AddObserver(*this);
  views_->themeView.AddObserver(*this);
  views_->themeImportView.AddObserver(*this);
  views_->projectView.AddObserver(*this);
  views_->importView.AddObserver(*this);
  views_->instrumentImportView.AddObserver(*this);
  views_->fm6PresetBrowserView.AddObserver(*this);
  views_->instrumentView.AddObserver(*this);
  views_->voiceView.AddObserver(*this);

  views_->tableView.AddObserver(*this);
  views_->grooveView.AddObserver(*this);
  views_->selectProjectView.AddObserver(*this);
  views_->mixerView.AddObserver(*this);
  views_->masterEffectsView.AddObserver(*this);
  views_->sampleEditorView.AddObserver(*this);
  views_->sampleSlicesView.AddObserver(*this);
  views_->recordView.AddObserver(*this);
  views_->creditsView.AddObserver(*this);

  memset(_charScreen, ' ', display_text_layout()->cells);
  memset(_preScreen, ' ', display_text_layout()->cells);
  memset(_charScreenProp, 0, display_text_layout()->cells);
  memset(_preScreenProp, 0, display_text_layout()->cells);

  Redraw();

  // there is some sort of race that if we call LoadProject() from here directly
  // causes audio init to fail, so instead set this flag which will then cause
  // LoadProject() to be called from within the next time that AnimationUpdate()
  // is called
  loadProject_ = true;
};

AppWindow::~AppWindow() { MidiService::GetInstance()->Close(); }

void AppWindow::SetSdCardPresent(bool present) {
  sdCardMissing_ = !present;
  if (!present) {
    sdCardMessageShown_ = false;
  }
  SetDirty();
}

bool AppWindow::GetSongSelectionRect(GUIRect &selectionRect) const {
  if (views_ == nullptr) {
    return false;
  }

  return views_->songView.GetActiveSelectionRect(selectionRect);
}

bool AppWindow::BuildSongSelectionRenderRequest(
    SelectionResampleRequest &request,
    SelectionOutputIntent outputIntent) const {
  if (views_ == nullptr) {
    return false;
  }

  return views_->songView.BuildSelectionRenderRequest(request, outputIntent);
}

void appwindow_set_sdcard_present(bool present) {
  if (instance) {
    instance->SetSdCardPresent(present);
  }
}

namespace {
void RequireDrawingLayout(bool ok) {
  if (ok)
    return;
  NAssert(ok && "Invalid drawing region or overlay capacity");
  static const char *const details[] = {"Drawing region capacity",
                                        "Check screen layout"};
  critical_error_message(&g_deadBitmap, "Display error", details, 2,
                         DEFAULT_ERROR_MESSAGE_DELAY_SEC);
}
const TextRegionInfo overlayTextRegion = {
    {8, 0, 704, 720, TEXT_FONT_NORMAL}, 0, 660, 8, 22, 30, 32, 24};
} // namespace

void AppWindow::FocusView(View &view, ViewType type) {
  const TextLayout layout = view.GetTextLayout();
  // A rejected layout leaves the previous regions active. Do not let the new
  // view draw into them; report the configuration error before giving it focus.
  RequireDrawingLayout(ConfigureTextRegions(layout.regions, layout.count));
  ConfigureGraphics(view.GetGraphicsLayout());
  view.SetFocus(type);
}

uint32_t AppWindow::GraphicsRevision() const {
  return display_graphics_revision();
}
void AppWindow::ConfigureGraphics(GraphicsLayout layout) {
  const uint32_t previousRevision = display_graphics_revision();
  RequireDrawingLayout(display_configure_graphics(layout));
  // Ownership changes dirty every backend text cell. Invalidate the matching
  // application cache so Flush also restores rounded corners after glyph DMA.
  // Reapplying an unchanged graphics layout must leave both caches intact.
  if (display_graphics_revision() != previousRevision)
    InvalidateTextCache();
}
void AppWindow::DrawGraphicsRect(uint8_t region, const GUIRect &local,
                                 ColorDefinition color) {
  GraphicsLayout layout = display_graphics_layout();
  RequireDrawingLayout(region < layout.count);
  const GraphicsRegion &r = layout.regions[region];
  int left = std::max(0, local.Left()), top = std::max(0, local.Top());
  int right = std::min(int(r.width), local.Right()),
      bottom = std::min(int(r.height), local.Bottom());
  if (right <= left || bottom <= top)
    return;
  GUIRect pixels(r.x + left, r.y + top, r.x + right, r.y + bottom);
  GUIWindow::SetCurrentRectColor(GetColor(color));
  GUIWindow::DrawRect(pixels);
}
void AppWindow::DiscardOverlays() {
  overlayDepth_ = overlayTarget_ = 0;
  display_discard_overlays();
}
bool AppWindow::HasOverlay(uint32_t owner) const {
  for (uint8_t i = 0; i < overlayDepth_; ++i)
    if (overlays_[i].owner == owner && overlays_[i].placed)
      return true;
  return false;
}
void AppWindow::BeginOverlay(uint32_t owner) {
  RequireDrawingLayout(overlayTarget_ == 0);
  uint8_t i = 0;
  for (; i < overlayDepth_; ++i)
    if (overlays_[i].owner == owner)
      break;
  if (i == overlayDepth_) {
    // Only a NEW main modal displaces a countdown; existing lower modals can
    // keep updating beneath it without capture/restore on every UI tick.
    if (owner != PowerOverlayOwner && overlayDepth_ &&
        overlays_[overlayDepth_ - 1].owner == PowerOverlayOwner) {
      CloseOverlay(PowerOverlayOwner);
      i = overlayDepth_;
    }
    Flush(); // Commit base text once, before capturing a newly opened window.
    RequireDrawingLayout(i < GRAPHICS_OVERLAY_LIMIT);
    OverlayCache &o = overlays_[i];
    o.owner = owner;
    o.placed = false;
    memset(o.chars, ' ', sizeof(o.chars));
    memset(o.props, 0, sizeof(o.props));
    memset(o.previous, 0xff, sizeof(o.previous));
    memset(o.previousProps, 0xff, sizeof(o.previousProps));
    ++overlayDepth_;
  }
  overlayTarget_ = i + 1;
  display_select_target(overlayTarget_);
}
void AppWindow::SetOverlayBounds(GraphicsRegion bounds) {
  RequireDrawingLayout(overlayTarget_ > 0);
  OverlayCache &o = overlays_[overlayTarget_ - 1];
  if (o.placed && !memcmp(&o.bounds, &bounds, sizeof(bounds)))
    return;
  if (o.placed) {
    uint32_t owner = o.owner;
    EndOverlay();
    CloseOverlay(owner);
    BeginOverlay(owner);
  }
  OverlayCache &next = overlays_[overlayTarget_ - 1];
  RequireDrawingLayout(display_push_overlay(bounds));
  next.bounds = bounds;
  next.placed = true;
  display_select_target(overlayTarget_);
}
void AppWindow::EndOverlay() {
  if (!overlayTarget_)
    return;
  if (overlays_[overlayTarget_ - 1].placed)
    Flush();
  else if (overlayTarget_ == overlayDepth_)
    --overlayDepth_;
  overlayTarget_ = 0;
  display_select_target(0);
}
void AppWindow::CloseModalOverlay() {
  // Modal factories reuse static storage, so the old object's instance ID may
  // already have changed by the time its replacement reaches View::DoModal.
  for (uint8_t i = 0; i < overlayDepth_; ++i) {
    if (overlays_[i].owner != PowerOverlayOwner) {
      CloseOverlay(overlays_[i].owner);
      return;
    }
  }
}
void AppWindow::CloseOverlay(uint32_t owner) {
  EndOverlay();
  uint8_t i = 0;
  for (; i < overlayDepth_; ++i)
    if (overlays_[i].owner == owner)
      break;
  if (i == overlayDepth_)
    return;
  Flush(); // Hidden base updates must reach backing storage before restoration.
  while (overlayDepth_ > i) {
    if (overlays_[--overlayDepth_].placed)
      display_pop_overlay();
  }
}

bool AppWindow::ConfigureTextRegions(const TextRegion *regions, uint8_t count) {
  const bool built = display_configure_text_regions(regions, count);
  if (!built)
    return false;
  // The physical clear establishes blank cells in both caches. Marking every
  // cell invalid here would add redundant blank-glyph transfers on screen
  // entry.
  Clear(true);
  DiscardOverlays();
  GUIWindow::Clear(backgroundColor_, true);
  return true;
}

const TextRegionInfo *AppWindow::GetTextRegion(uint8_t region) const {
  const TextRegionLayout *layout = display_text_layout();
  return region < layout->count ? &layout->regions[region] : nullptr;
}

void AppWindow::DrawRegionString(uint8_t region, const char *text,
                                 const GUIPoint &pos,
                                 const GUITextProperties &props) {
  const TextRegionLayout *layout = display_text_layout();
  if (region >= layout->count)
    return;
  // Ensure color index is masked to prevent overlap with inversion bit.
  uint8_t property = (colorIndex_ & 0x7f) | (props.invert_ ? PROP_INVERT : 0);
  text_region_write(&layout->regions[region], _charScreen, _charScreenProp,
                    pos._x, pos._y, text, property);
}

void AppWindow::DrawString(const char *string, const GUIPoint &pos,
                           const GUITextProperties &props, bool force) {
  // Safety check for null string.
  if (!string)
    return;
  if (overlayTarget_) {
    OverlayCache &o = overlays_[overlayTarget_ - 1];
    text_region_write(&overlayTextRegion, o.chars, o.props, pos._x, pos._y,
                      string,
                      (colorIndex_ & 0x7f) | (props.invert_ ? PROP_INVERT : 0));
    return;
  }
  // Base coordinates are local to the explicitly selected region.
  DrawRegionString(props.region_, string, pos, props);
}

void AppWindow::Clear(bool all) {
  if (overlayTarget_) {
    OverlayCache &o = overlays_[overlayTarget_ - 1];
    memset(o.chars, ' ', sizeof(o.chars));
    memset(o.props, 0, sizeof(o.props));
    if (all) {
      memset(o.previous, ' ', sizeof(o.previous));
      memset(o.previousProps, 0, sizeof(o.previousProps));
    }
    return;
  }
  memset(_charScreen, ' ', display_text_layout()->cells);
  memset(_charScreenProp, 0, display_text_layout()->cells);
  if (all) {
    memset(_preScreen, ' ', display_text_layout()->cells);
    memset(_preScreenProp, 0, display_text_layout()->cells);
  };
};

void AppWindow::StyleRegionTextRect(uint8_t region, const GUIRect &rect,
                                    ColorDefinition color, bool invert) {
  const TextRegionInfo *info = GetTextRegion(region);
  if (!info)
    return;
  text_region_style(info, _charScreenProp, rect.Left(), rect.Top(),
                    rect.Right(), rect.Bottom(),
                    (uint8_t(color) & 0x7f) | (invert ? PROP_INVERT : 0));
}

void AppWindow::ClearRegionTextRect(uint8_t region, const GUIRect &rect) {
  const TextRegionLayout *layout = display_text_layout();
  if (region >= layout->count)
    return;
  const TextRegionInfo &r = layout->regions[region];
  int left = std::max(0, int(rect.Left()));
  int right = std::min(int(r.columns), int(rect.Right()));
  if (left >= right)
    return;
  for (int row = std::max(0, int(rect.Top()));
       row < std::min(int(r.rows), int(rect.Bottom())); ++row) {
    uint16_t index = r.offset + row * r.columns + left;
    memset(_charScreen + index, ' ', right - left);
    memset(_charScreenProp + index, 0, right - left);
  }
}

void AppWindow::ClearTextRect(GUIRect &r) {
  if (overlayTarget_) {
    OverlayCache &o = overlays_[overlayTarget_ - 1];
    int left = std::max(0, r.Left()), right = std::min(32, r.Right());
    if (left >= right)
      return;
    for (int y = std::max(0, r.Top()); y < std::min(24, r.Bottom()); ++y) {
      memset(o.chars + y * 32 + left, ' ', right - left);
      memset(o.props + y * 32 + left, 0, right - left);
    }
    return;
  }
  // Base text uses ClearRegionTextRect with an explicit region ID.
}

void AppWindow::InvalidateTextCache() {
  // Force the next text flush to resend all cells without changing the current
  // text buffer contents.
  memset(_preScreen, 0xFF, display_text_layout()->cells);
  memset(_preScreenProp, 0xFF, display_text_layout()->cells);
}

//
// Flush current screen to display
//
void AppWindow::Flush() {
  // Per-region glyph DMA and immediate overlay drawing happen here, before
  // the backend's final Flush sees the remaining queue. Measure the complete
  // text flush, including cache checks, corner painting and USB submission.
  ScopedDWTProfile graphicsBackendProfile(&gGraphicsBackendProfile);
  Lock();

  GUITextProperties props;
  GUIPoint pos;

  // Start with an invalid color to force color setting on first character
  ColorDefinition color = (ColorDefinition)-1;
  pos._x = 0;
  pos._y = 0;

  int count = 0;

  unsigned char *current = _charScreen;
  unsigned char *previous = _preScreen;
  unsigned char *currentProp = _charScreenProp;
  unsigned char *previousProp = _preScreenProp;
  const TextRegionLayout *layout = display_text_layout();
  OverlayCache *overlay =
      overlayTarget_ ? &overlays_[overlayTarget_ - 1] : nullptr;
  if (overlay && !overlay->placed) {
    Unlock();
    return;
  }
  for (uint8_t region = 0; region < (overlay ? 1 : layout->count); ++region) {
    const TextRegionInfo &r =
        overlay ? overlayTextRegion : layout->regions[region];
    props.region_ = region;
    // One reusable bitmap, including the fixed overlay grid. Keep the old
    // properties intact until boundary dependencies have been recorded.
    uint8_t dirty[(std::max(TEXT_REGION_CAPACITY, 32 * 24) + 7) / 8];
    const uint8_t *regionProps =
        overlay ? overlay->props : _charScreenProp + r.offset;
    int top = overlay ? overlay->bounds.y / 30 : 0;
    int bottom =
        overlay
            ? std::min(24,
                       (overlay->bounds.y + overlay->bounds.height + 29) / 30)
            : r.rows;
    int left = overlay ? std::max(0, (overlay->bounds.x - 8) / 22) : 0;
    int right =
        overlay
            ? std::min(
                  32, (overlay->bounds.x + overlay->bounds.width - 8 + 21) / 22)
            : r.columns;
    // Match dirty scanning to the glyph/corner painting bounds. Small overlays
    // retain full-grid storage but no longer scan all 768 cells each flush.
    TextHighlightChanges(
        overlay ? overlay->chars : _charScreen + r.offset, regionProps,
        overlay ? overlay->previous : _preScreen + r.offset,
        overlay ? overlay->previousProps : _preScreenProp + r.offset, r.columns,
        r.rows, dirty, left, top, right, bottom);
    pos._y = top;
    for (int y = top; y < bottom; ++y) {
      pos._x = left;
      if (overlay) {
        int at = y * 32 + left;
        current = overlay->chars + at;
        previous = overlay->previous + at;
        currentProp = overlay->props + at;
        previousProp = overlay->previousProps + at;
      }
      for (int x = left; x < right; ++x) {
#ifndef _LGPT_NO_SCREEN_CACHE_
        if (dirty[(y * r.columns + x) / 8] &
            (1U << ((y * r.columns + x) % 8))) {
#endif
          // Extract invert flag from properties
          props.invert_ = (*currentProp & PROP_INVERT) != 0;

          // Extract color index from properties and check if it's different
          // from current color
          ColorDefinition charColor = (ColorDefinition)((*currentProp) & 0x7F);
          if (charColor != color) {
            color = charColor;

            // Initialize gcolor with a safe default to avoid uninitialized
            // value if switch falls through
            GUIColor gcolor = normalColor_;
            switch (color) {
            case CD_BACKGROUND:
              gcolor = backgroundColor_;
              break;
            case CD_NORMAL:
              break;
            case CD_HILITE1:
              gcolor = highlightColor_;
              break;
            case CD_HILITE2:
              gcolor = highlight2Color_;
              break;
            case CD_CONSOLE:
              gcolor = consoleColor_;
              break;
            case CD_CURSOR:
              gcolor = cursorColor_;
              break;
            case CD_INFO:
              gcolor = infoColor_;
              break;
            case CD_WARN:
              gcolor = warnColor_;
              break;
            case CD_ERROR:
              gcolor = errorColor_;
              break;
            case CD_ACCENT:
              gcolor = accentColor_;
              break;
            case CD_ACCENTALT:
              gcolor = accentAltColor_;
              break;
            case CD_EMPHASIS:
              gcolor = emphasisColor_;
              break;
            case CD_RESERVED1:
              gcolor = reserved1Color_;
              break;
            case CD_RESERVED2:
              gcolor = reserved2Color_;
              break;
            case CD_RESERVED3:
              gcolor = reserved3Color_;
              break;
            case CD_RESERVED4:
              gcolor = reserved4Color_;
              break;

            default:
              NAssert(0);
              break;
            }
            GUIWindow::SetColor(gcolor);
          }
          GUIWindow::DrawChar(*current, pos, props);
          count++;
          // Update only changed cells; avoid copying the entire cache every
          // frame.
          *previous = *current;
          *previousProp = *currentProp;
#ifndef _LGPT_NO_SCREEN_CACHE_
        }
#endif
        current++;
        previous++;
        currentProp++;
        previousProp++;
        ++pos._x;
      }
      ++pos._y;
      pos._x = 0;
    }
    // Complete opaque glyph DMA before trimming corners. Only changed cells
    // are visited for painting; unchanged highlights generate no USB traffic.
    display_draw_changed();
    bool cornerColorSet = false;
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        const uint16_t at = y * r.columns + x;
#ifndef _LGPT_NO_SCREEN_CACHE_
        if (!(dirty[at / 8] & (1U << (at % 8))))
          continue;
#endif
        const uint8_t corners =
            TextHighlightCorners(regionProps, r.columns, r.rows, x, y);
        if (!corners)
          continue;
        const GraphicsRegion cell = {
            int16_t(r.definition.x + x * r.glyph_width),
            int16_t(r.definition.y + y * r.glyph_height), r.glyph_width,
            r.glyph_height};
        // Match the glyph renderer: graphics own any intersecting whole cell.
        bool graphCell = false;
        if (!overlay) {
          const GraphicsLayout graphs = display_graphics_layout();
          for (uint8_t i = 0; i < graphs.count; ++i) {
            const GraphicsRegion hit =
                pixel_intersection(cell, graphs.regions[i]);
            graphCell |= hit.width && hit.height;
          }
        }
        if (graphCell)
          continue;
        if (!cornerColorSet) {
          GUIWindow::SetColor(backgroundColor_);
          cornerColorSet = true;
          color = (ColorDefinition)-1;
        }
        TextHighlightPaint(corners, cell.x, cell.y, cell.width, cell.height,
                           [this](int px, int py, int width, int height) {
                             GUIRect rect(px, py, px + width, py + height);
                             // Existing solid spans route to live overlay
                             // backing and remote UI without pixel readback.
                             GUIWindow::DrawRect(rect);
                           });
      }
    }
  }
  GUIWindow::Flush();
  Unlock();
};

AppWindow::LoadProjectResult AppWindow::LoadProject(const char *projectName) {

  _closeProject = false;

  PersistencyService *persist = PersistencyService::GetInstance();

  Player *player = Player::GetInstance();
  // Loading resets and destroys project-owned instruments and sample data.
  // Hard-stop every voice first so the audio task cannot retain references to
  // that state through release tails or direct-note audition.
  player->Kill();

  TablePlayback::Reset();
  TableHolder::GetInstance()->Reset();
  Mixer::GetInstance()->Clear();

  SamplePool *pool = SamplePool::GetInstance();
  pool->Reset();

  project_.Load(projectName);
  Project *project = &project_;

  if (createProjectOnLoad_) {
    PersistencyResult created = persist->CreateProject();
    if (created != PERSIST_SAVED) {
      Trace::Error("Failed to create new project '%s'", projectName);
      return LoadProjectResult::LOAD_FAILED;
    }
    createProjectOnLoad_ = false;
  }

  // load the projects samples
  pool->Load(projectName);

  bool succeeded = (persist->Load(projectName) == PERSIST_LOADED);
  if (!succeeded) {
    Trace::Error("Failed to load project '%s'", projectName);
    pool->Reset();
    TableHolder::GetInstance()->Reset();
    return LoadProjectResult::LOAD_FAILED;
  };

  // Project

  WatchedVariable::Disable();

  // Register as an observer of the project name variable to get notified of
  // changes
  Variable *projectNameVar = project->FindVariable(FourCC::VarProjectName);
  if (projectNameVar) {
    WatchedVariable *watchedVar = (WatchedVariable *)projectNameVar;
    if (watchedVar) {
      watchedVar->RemoveObserver(*this);
      watchedVar->AddObserver(*this);
      // Store the initial project name
      project->GetProjectName(projectName_);
    }
  }

  project->GetInstrumentBank()->Init();

  WatchedVariable::Enable();

  ApplicationCommandDispatcher::GetInstance()->Init(project);

  // Update view data
  viewData_.Load(project);

  // Program changes are runtime state. Restore each MIDI channel's project
  // default whenever a project is loaded.
  MidiService::GetInstance()->LoadProjectSettings(*project);

  if (views_) {
    views_->songView.Reset();
    views_->chainView.Reset();
    views_->phraseView.Reset();
    views_->grooveView.Reset();
    views_->tableView.Reset();
    views_->projectView.Reset();
    views_->instrumentView.Reset();
    views_->voiceView.Reset();
    views_->mixerView.Reset();
    views_->masterEffectsView.Reset();
    views_->importView.Reset();
    views_->instrumentImportView.Reset();
    views_->fm6PresetBrowserView.Reset();
    views_->themeView.Reset();
    views_->themeImportView.Reset();
    views_->selectProjectView.Reset();
    views_->sampleEditorView.Reset();
    views_->sampleSlicesView.Reset();
    views_->recordView.Reset();
  }

  bool playerOK = true;
  if (!playerInitialized_) {
    playerOK = player->Init(project, &viewData_);
    player->AddObserver(*this);
    playerInitialized_ = true;
  } else {
    player->BindProject(project, &viewData_);
  }

  // Create the controller
  UIController *controller = UIController::GetInstance();
  controller->Init(project, &viewData_);

  _currentView = &views_->songView;
  FocusView(*_currentView, VT_SONG);

  MidiService::GetInstance()->ReloadControlSurface();

  if (!playerOK) {
    MessageBox *mb = MessageBox::Create(views_->songView,
                                        "Failed to initialize audio", MBBF_OK);
    views_->songView.DoModal(mb);
  }

  if (_currentView) {
    _currentView->SetDirty(true);
    SetDirty();
  }

  if (persist->SaveProjectState(projectName) != PERSIST_SAVED) {
    Trace::Error("Failed to save project state for '%s'", projectName);
  }
  return LoadProjectResult::LOAD_OK;
}

void AppWindow::CloseProject() {

  _closeProject = false;
  Player *player = Player::GetInstance();
  player->Stop();
  player->RemoveObserver(*this);

  // Release view-specific state before resetting the project resources.
  if (_currentView)
    _currentView->LooseFocus();

  SamplePool *pool = SamplePool::GetInstance();
  pool->Reset();

  TableHolder::GetInstance()->Reset();
  TablePlayback::Reset();

  ApplicationCommandDispatcher::GetInstance()->Close();

  UIController *controller = UIController::GetInstance();
  controller->Reset();

  _currentView = &views_->nullView;
  // Use the same entry path as other screens: install the text/graphics
  // layouts and discard overlay backing belonging to the previous screen.
  FocusView(*_currentView, VT_NULL);
  views_->nullView.SetDirty(true);
};

AppWindow *AppWindow::Create(GUICreateWindowParams &params,
                             const char *projectName) {
  I_GUIWindowImp &imp =
      I_GUIWindowFactory::GetInstance()->CreateWindowImp(params);
  alignas(AppWindow) static char appWindowMemBuf[sizeof(AppWindow)];
  AppWindow *w = new (appWindowMemBuf) AppWindow(imp, projectName);
  return w;
};

AppWindow *AppWindow::GetInstance() { return instance; }

void AppWindow::SetDirty() {
  if (_currentView) {
    _currentView->SetDirty(true);
  }
};

void AppWindow::SetMixerViewDirty() {
  if (views_ && _currentView == &views_->mixerView) {
    _currentView->SetDirty(true);
  }
};

void AppWindow::UpdateColorsFromConfig() {
  // now assign custom colors if they have been set device config
  defineColor(FourCC::VarBGColor, backgroundColor_, 0);
  defineColor(FourCC::VarFGColor, normalColor_, 1);
  defineColor(FourCC::VarHI1Color, highlightColor_, 2);
  defineColor(FourCC::VarHI2Color, highlight2Color_, 3);
  defineColor(FourCC::VarCursorColor, cursorColor_, 4);
  defineColor(FourCC::VarConsoleColor, consoleColor_, 5);
  defineColor(FourCC::VarInfoColor, infoColor_, 6);
  defineColor(FourCC::VarWarnColor, warnColor_, 7);
  defineColor(FourCC::VarErrorColor, errorColor_, 8);
  defineColor(FourCC::VarAccentColor, accentColor_, 9);
  defineColor(FourCC::VarAccentAltColor, accentAltColor_, 10);
  defineColor(FourCC::VarEmphasisColor, emphasisColor_, 11);

  // These are commented out so they are not included in config or theme exports
  // until they are actually used in the future
  // defineColor(FourCC::VarReserved1Color, reserved1Color_, 12);
  // defineColor(FourCC::VarReserved2Color, reserved2Color_, 13);
  // defineColor(FourCC::VarReserved3Color, reserved3Color_, 14);
  // defineColor(FourCC::VarReserved4Color, reserved4Color_, 15);
};

bool AppWindow::onEvent(GUIEvent &event) {

  // We need to tell the app to quit once we're out of the
  // mixer lock, otherwise the windows driver will never return

  _shouldQuit = false;

  unsigned short v = 1 << event.GetValue();

  MixerService *sm = MixerService::GetInstance();
  // TODO(democloid): this causes a deadlock, verify original intent
  //  MixerService *ms = MixerService::GetInstance();
  //  ms->Lock();

  switch (event.GetType()) {

  case ET_PADBUTTONDOWN:

    _mask |= v;
    if (_currentView)
      _currentView->ProcessButton(BuildButtonEventMask(_mask, v, true), _mask,
                                  true);
    break;

  case ET_PADBUTTONUP:

    _mask &= (0xFFFF - v);
    if (_currentView)
      _currentView->ProcessButton(BuildButtonEventMask(_mask, v, false), _mask,
                                  false);
    break;

  case ET_SYSQUIT:
    _shouldQuit = true;
    break;

    /*		case ET_KEYDOWN:
            if
       (event.GetValue()==EKT_ESCAPE&&!Player::GetInstance()->IsRunning()) {
       if
       (_currentView!=_listView) {
       CloseProject() ;
       _currentView->SetDirty(true) ;
       } else {
                            System::GetInstance()->PostQuitMessage() ;
                    };
            } ;
                */

  default:
    break;
  }
  //  ms->Unlock();

  if (_shouldQuit) {
    onQuitApp();
  }
  if (_closeProject) {
    CloseProject();
    SetDirty();
  }

  // View dirty flag will be checked in AnimationUpdate to determine if redraw
  // is needed
  return false;
};

void AppWindow::onUpdate(bool redraw) {
  if (redraw) {
    DiscardOverlays();
    GUIWindow::Clear(backgroundColor_, true);
    Clear(true);
    // Mark as dirty to trigger redraw in AnimationUpdate
    SetDirty();
  }
  // No Flush here - AnimationUpdate will handle it
};

void AppWindow::AnimationUpdate() {
  // Increment the animation frame counter
  animationFrameCounter_++;

  if (awaitingProjectLoadAck_) {
    if (_mask != 0) {
      FileSystem::GetInstance()->DeleteFile("/.current");
      npf_snprintf(projectName_, sizeof(projectName_), "%s",
                   UNNAMED_PROJECT_NAME);
      originalProjectName_ = UNNAMED_PROJECT_NAME;
      loadProject_ = true;
      awaitingProjectLoadAck_ = false;
      Trace::Error("Falling back to untitled after failed project load");
    }
    return;
  }

  if (loadProject_) {
    const etl::string<MAX_PROJECT_NAME_LENGTH> projectNameToLoad(projectName_);
    const bool loadingUnnamedProject =
        strcmp(projectNameToLoad.c_str(), UNNAMED_PROJECT_NAME) == 0;
    LoadProjectResult loadResult = LoadProject(projectNameToLoad.c_str());
    loadProject_ = false;
    if (loadResult == LoadProjectResult::LOAD_FAILED) {
      createProjectOnLoad_ = loadingUnnamedProject;
      Status::SetMultiLine(
          "Invalid Project:\n%s\n  \nPress any key\nto continue...",
          projectNameToLoad.c_str());
      Trace::Error(
          "Failed to load project '%s'. Waiting for key press to load untitled",
          projectNameToLoad.c_str());
      awaitingProjectLoadAck_ = true;
      return;
    }
  }

  // run at 1Hz
  if (animationFrameCounter_ % PICO_CLOCK_HZ == 0) {
    // Check battery level
    BatteryState batteryState;
    System::GetInstance()->GetBatteryState(batteryState);
    if (!batteryState.error) {
      // only process battery status if no error state
      if (batteryState.percentage < MINIMUM_ALLOWED_BATTERY_PERCENTAGE &&
          !batteryState.charging) {
        lowBatteryState_ = true;
        lowBatteryWarningCounter_++;
      } else {
        lowBatteryState_ = false;
        lowBatteryWarningCounter_ = 0;
      }
      if (lowBatteryWarningCounter_ > MIN_BATT_POWEROFF_SEC) {
        PowerDown();
      }
    }
  }

  // A view change or programmatic dismissal may have removed the warning.
  // Never treat a missing modal as a warning that is still on screen.
  if (!_currentView || !_currentView->HasModalView()) {
    lowBatteryMessageShown_ = false;
    sdCardMessageShown_ = false;
  }

  if (lowBatteryState_ && !lowBatteryMessageShown_) {
    if (_currentView && !_currentView->HasModalView()) {
      FullScreenBox *mb =
          FullScreenBox::Create(*_currentView, &g_lowBatteryBitmap);
      _currentView->DoModal(mb);
      lowBatteryMessageShown_ = true;
      SetDirty();
    }
  } else if (!lowBatteryState_ && lowBatteryMessageShown_) {
    ModalView *modal = _currentView->GetModalView();
    if (modal) {
      modal->EndModal(0);
      _currentView->DismissModal();
      Trace::Debug("CLose Low Batt dialog");
    }
    lowBatteryMessageShown_ = false;
    SetDirty();
  }

  if (sdCardMissing_ && !sdCardMessageShown_) {
    if (_currentView) {
      FullScreenBox *mb =
          FullScreenBox::Create(*_currentView, &g_sdCardMissingBitmap);
      _currentView->DoModal(mb);
      sdCardMessageShown_ = true;
      SetDirty();
    }
  } else if (!sdCardMissing_ && sdCardMessageShown_) {
    ModalView *modal = _currentView ? _currentView->GetModalView() : nullptr;
    if (modal) {
      modal->EndModal(0);
      _currentView->DismissModal();
    }
    sdCardMessageShown_ = false;
    SetDirty();
  }

  // If we need a full redraw due to state changes from key events
  {
    // Measure the full UI render pass for this clock tick, including view
    // redraw work such as graph rectangles plus the final display flush.
    ScopedDWTProfile graphicsFlushProfile(&gGraphicsFlushProfile);

    if (sdCardMissing_ && sdCardMessageShown_) {
      ModalView *modal = _currentView ? _currentView->GetModalView() : nullptr;
      if (modal && _currentView->isDirty()) {
        BeginOverlay(modal->GetInstanceId());
        SetOverlayBounds({0, 0, 720, 720});
        Clear(true);
        drawFullscreenBitmap(g_sdCardMissingBitmap);
        EndOverlay();
        _currentView->SetDirty(false);
      }
    } else if (lowBatteryState_ && lowBatteryMessageShown_) {
      ModalView *modal = _currentView ? _currentView->GetModalView() : nullptr;
      if (modal && _currentView->isDirty()) {
        BeginOverlay(modal->GetInstanceId());
        SetOverlayBounds({0, 0, 720, 720});
        Clear(true);
        drawFullscreenBitmap(g_lowBatteryBitmap);
        EndOverlay();
        _currentView->SetDirty(false);
      }
    } else {
      if (_currentView && _currentView->isDirty()) {
        ConfigureGraphics(_currentView->GetGraphicsLayout());
        _currentView->Redraw(); // Draw main content
      }

      // Handle view updates
      if (_currentView) {
        // Always update the main view even if modal is active because things
        // like batt gauge still need redrawing and visibility even with a
        // modal onscreen
        _currentView->AnimationUpdate();
        // Now check if there's an active modal view and
        ModalView *modalView = _currentView->GetModalView();
        if (modalView) {
          // Update the modal view
          const bool redrawModal =
              modalView->isDirty() || !HasOverlay(modalView->GetInstanceId());
          BeginOverlay(modalView->GetInstanceId());
          if (redrawModal)
            modalView->Redraw();
          modalView->AnimationUpdate();
          EndOverlay();
          // Modal can complete from animation updates (e.g. timed hold
          // confirms).
          _currentView->DismissModal();
        }
      }

      // Always flush after AnimationUpdate to ensure consistent state
      Flush();
    }
  }

  // *attempt* to auto save every AUTOSAVE_INTERVAL_IN_SECONDS
  // will return false if auto save was unsuccessful because eg. the sequencer
  // is running
  // we do this here because for sheer convenience because this
  // this callback is called PICO_CLOCK_HZ times a second and we have easy
  // access in this class to the player, projectname and persistence service
  if ((++lastAutoSave / PICO_CLOCK_HZ) > AUTOSAVE_INTERVAL_IN_SECONDS) {
    if (AutoSave()) {
      lastAutoSave = 0;
    }
  }
}

void AppWindow::LayoutChildren(){};

void AppWindow::Update(Observable &o, I_ObservableData *d) {
  if (d && (uintptr_t)d == (uintptr_t)FourCC::VarProjectName) {
    // Update the stored project name from the project
    Project *project = viewData_.project_;
    if (project) {
      project->GetProjectName(projectName_);
      Trace::Log("APPWINDOW", "Project name retrieved: %s", projectName_);
    } else {
      Trace::Error("APPWINDOW: Project name retrieval failed!");
    }
    return;
  }

  ViewEvent *ve = (ViewEvent *)d;

  switch (ve->GetType()) {

  case VET_SWITCH_VIEW: {
    ViewType *vt = (ViewType *)ve->GetData();
    if (_currentView) {
      _currentView->LooseFocus();
    }

    switch (*vt) {
    case VT_SONG:
      _currentView = &views_->songView;
      break;
    case VT_CHAIN:
      _currentView = &views_->chainView;
      break;
    case VT_PHRASE:
      _currentView = &views_->phraseView;
      break;
    case VT_DEVICE:
      _currentView = &views_->deviceView;
      break;
    case VT_PROJECT:
      _currentView = &views_->projectView;
      break;
    case VT_INSTRUMENT:
      _currentView = &views_->instrumentView;
      break;
    case VT_VOICE:
      _currentView = &views_->voiceView;
      break;

    case VT_TABLE:
      _currentView = &views_->tableView;
      break;
    case VT_TABLE2:
      _currentView = &views_->tableView;
      break;
    case VT_GROOVE:
      _currentView = &views_->grooveView;
      break;
    case VT_IMPORT:
      _currentView = &views_->importView;
      break;
    case VT_INSTRUMENT_IMPORT:
      _currentView = &views_->instrumentImportView;
      break;
    case VT_FM6_PRESETS:
      _currentView = &views_->fm6PresetBrowserView;
      break;
    case VT_SELECTPROJECT:
      _currentView = &views_->selectProjectView;
      break;
    case VT_MIXER:
      _currentView = &views_->mixerView;
      break;
    case VT_MASTER_EFFECTS:
      _currentView = &views_->masterEffectsView;
      break;
    case VT_THEME:
      _currentView = &views_->themeView;
      break;
    case VT_THEME_IMPORT:
      _currentView = &views_->themeImportView;
      break;
    case VT_SELECTTHEME:
      _currentView = &views_->themeView;
      break;
    case VT_SAMPLE_EDITOR:
      _currentView = &views_->sampleEditorView;
      break;
    case VT_SAMPLE_SLICES:
      _currentView = &views_->sampleSlicesView;
      break;
    case VT_RECORD:
      _currentView = &views_->recordView;
      break;
    case VT_CREDITS:
      _currentView = &views_->creditsView;
      break;
    default:
      break;
    }
    FocusView(*_currentView, *vt);
    SetDirty();
    break;
  }

  case VET_PLAYER_POSITION_UPDATE: {
    PlayerEvent *pt = (PlayerEvent *)ve;
    if (_currentView) {
      // Playback continues behind dialogs. Keep the base view's event-driven
      // indicators current; its animation pass also updates live backing.
      _currentView->OnPlayerUpdate(pt->GetType(), pt->GetTickCount());
      // Check for a modal after updating the base view, then notify it too.
      if (ModalView *modal = _currentView->GetModalView())
        modal->OnPlayerUpdate(pt->GetType(), pt->GetTickCount());
    }
    break;
  }

  case VET_LOAD_PROJECT: {
    const char *name = static_cast<const char *>(ve->GetData());
    if (name && name[0] != '\0') {
      npf_snprintf(projectName_, sizeof(projectName_), "%s", name);
      originalProjectName_ = name;
      createProjectOnLoad_ = false;
      loadProject_ = true;
    }
    break;
  }
  case VET_NEW_PROJECT: {
    npf_snprintf(projectName_, sizeof(projectName_), "%s",
                 UNNAMED_PROJECT_NAME);
    originalProjectName_ = UNNAMED_PROJECT_NAME;
    createProjectOnLoad_ = true;
    loadProject_ = true;
    break;
  }
  case VET_QUIT_PROJECT: {
    // defer event to after we got out of the view
    _closeProject = true;
    break;
  }
  case VET_QUIT_APP:
    _shouldQuit = true;
    break;
  default: // VET_LIST_SELECT, VET_UPDATE
    break;
  }
}

void AppWindow::onQuitApp() {
  Player *player = Player::GetInstance();
  player->Stop();
  player->RemoveObserver(*this);

  player->Reset();
  System::GetInstance()->PostQuitMessage();
}

void AppWindow::Print(char *line) { PrintMultiLine(line); }

void AppWindow::PrintMultiLine(char *line) {
  // Status output belongs to the underlying screen, including when a modal
  // invokes a synchronous operation. Preserve the caller's drawing target.
  const bool resumeOverlay = overlayTarget_ != 0;
  const uint32_t owner =
      resumeOverlay ? overlays_[overlayTarget_ - 1].owner : 0;
  if (resumeOverlay)
    EndOverlay();
  Clear();
  GUITextProperties props;
  props.region_ = TEXT_REGION_BODY;
  const TextRegionInfo &body = display_text_layout()->regions[TEXT_REGION_BODY];
  SetColor(CD_NORMAL);

  int current_y = body.rows / 2 - 1; // Start near the middle of the screen

  // Handle single line case for Print function
  bool isSingleLine = (line != nullptr && strchr(line, '\n') == nullptr);

  // Use strtok to split the string by newline characters
  char *token = strtok(line, "\n");
  int lineCount = 0;

  while (token != NULL) {
    // Stop if we are about to overwrite the build string line
    if (current_y >= body.rows - 1) {
      break;
    }

    // Center a single line within the active body region.
    if (isSingleLine && lineCount == 0) {
      current_y = body.rows / 2;
    }

    // Horizontally center the current line of text
    int position = body.columns; // Use the active region's character width
    position -= strlen(token);
    position /= 2;

    GUIPoint pos(position, current_y);
    DrawString(token, pos, props);

    // Get the next line
    token = strtok(NULL, "\n");
    current_y++;
    lineCount++;
  }

  // Preserve the build string at the bottom of the screen
  const FirmwareVersionString buildString = FormatFirmwareRelease();
  GUIPoint pos(0, body.rows - 1);
  pos._x = (body.columns - buildString.size()) / 2;
  DrawString(buildString.c_str(), pos, props);
  Flush();
  if (resumeOverlay)
    BeginOverlay(owner);
};

void AppWindow::SetColor(ColorDefinition cd) {
  // Ensure color index is within valid range (0-15)
  if (cd <= CD_EMPHASIS) {
    colorIndex_ = cd;
  } else {
    Trace::Error("APPWINDOW", "Invalid color index: %d", cd);
    colorIndex_ = CD_NORMAL; // Default to normal color
  }
};

bool AppWindow::AutoSave() {
  Player *player = Player::GetInstance();
  if (views_ == nullptr || _currentView == nullptr) {
    return false;
  }
  // only auto save when sequencer is not running, not recording,
  // and the user is in an autosave-safe view.
  bool autosaveSafeView = _currentView == &views_->songView ||
                          _currentView == &views_->chainView ||
                          _currentView == &views_->phraseView ||
                          _currentView == &views_->tableView ||
                          _currentView == &views_->grooveView ||
                          _currentView == &views_->instrumentView ||
                          _currentView == &views_->voiceView ||
                          _currentView == &views_->deviceView ||
                          _currentView == &views_->themeView ||
                          _currentView == &views_->mixerView ||
                          _currentView == &views_->masterEffectsView;
  bool recording = IsRecordingActive();
  if (!player->IsRunning() && !recording && autosaveSafeView) {
    Trace::Log("APPWINDOW", "AutoSaving Project Data");
    // get persistence service and call autosave
    PersistencyService *ps = PersistencyService::GetInstance();
    auto result = ps->AutoSaveProjectData(projectName_);
    if (result != PERSIST_SAVED) {
      Trace::Error("APPWINDOW", "Failed to auto-save project data");
      // we dont return false here as we dont want to go into a bombardment of
      // auto save attempts and instead just attempt to auto save again after
      // the next interval
    }
    return true;
  }
  return false;
}

void AppWindow::PowerDown() {
  // Device and Theme defer config writes until focus is lost. Powering down
  // from either view bypasses normal navigation, so flush their pending
  // changes before saving the project and cutting power.
  View *viewToRestore = nullptr;
  ViewType viewTypeToRestore = VT_DEVICE;
  if (views_ != nullptr && (_currentView == &views_->deviceView ||
                            _currentView == &views_->themeView)) {
    viewToRestore = _currentView;
    viewTypeToRestore =
        (_currentView == &views_->deviceView) ? VT_DEVICE : VT_THEME;
    _currentView->LooseFocus();
  }

  AutoSaveBeforePowerDown();
  System::GetInstance()->PowerDown();

  // PowerDown normally cuts power and never returns. If it does return, keep
  // the displayed view active so settings can still be changed and saved.
  if (viewToRestore != nullptr) {
    FocusView(*viewToRestore, viewTypeToRestore);
  }
}

void AppWindow::AutoSaveBeforePowerDown() {
  // nothing to save if there is no project currently loaded, or if one is
  // still in the process of being loaded
  if (views_ == nullptr || _currentView == nullptr ||
      _currentView == &views_->nullView || loadProject_ ||
      awaitingProjectLoadAck_) {
    return;
  }

  // unlike the timed auto save we cant just skip saving when the sequencer is
  // running or we are recording because we are about to lose power, so instead
  // stop everything that could be writing to the sdcard or mutating the
  // project while we save it.
  // an in-progress recording is thrown away rather than saved
  CancelRecording();
  Player *player = Player::GetInstance();
  if (player != nullptr) {
    player->Kill();
  }

  Trace::Log("APPWINDOW", "AutoSaving Project Data before power down");
  // deliberately an auto save and *not* an explicit save so that we dont
  // clobber the users last explicit save of the project
  PersistencyService *ps = PersistencyService::GetInstance();
  if (ps->AutoSaveProjectData(projectName_) != PERSIST_SAVED) {
    Trace::Error("APPWINDOW", "Failed to auto-save project data at power down");
  }
}

// Maps the ColorDefinition used in View classes to a GUIColor that is needed by
// DrawRect which unlike the text grid based drawing code works in terms of
// GUIColor and not definitions
GUIColor AppWindow::GetColor(ColorDefinition cd) {
  switch (cd) {
  case CD_NORMAL:
    return AppWindow::normalColor_;
  case CD_BACKGROUND:
    return AppWindow::backgroundColor_;
  case CD_HILITE1:
    return AppWindow::highlightColor_;
  case CD_HILITE2:
    return AppWindow::highlight2Color_;
  case CD_CONSOLE:
    return AppWindow::consoleColor_;
  case CD_CURSOR:
    return AppWindow::cursorColor_;
  case CD_INFO:
    return AppWindow::infoColor_;
  case CD_WARN:
    return AppWindow::warnColor_;
  case CD_ERROR:
    return AppWindow::errorColor_;
  case CD_ACCENT:
    return AppWindow::accentColor_;
  case CD_ACCENTALT:
    return AppWindow::accentAltColor_;
  case CD_EMPHASIS:
    return AppWindow::emphasisColor_;
  case CD_RESERVED1:
    return AppWindow::reserved1Color_;
  case CD_RESERVED2:
    return AppWindow::reserved2Color_;
  case CD_RESERVED3:
    return AppWindow::reserved3Color_;
  case CD_RESERVED4:
    return AppWindow::reserved4Color_;

  default:
    break;
  }
  return AppWindow::normalColor_;
}
