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

#ifndef _APP_WINDOW_H_
#define _APP_WINDOW_H_

#include "Application/Player/SelectionResampleRequest.h"
#include "Application/ScreenConstants.h"
#include "Application/Views/BaseClasses/View.h"
#include "Application/Views/ViewData.h"
#include "Externals/etl/include/etl/string.h"
#include "Foundation/Observable.h"
#include "System/Process/SysMutex.h"
#include "System/io/Status.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include <UIFramework/SimpleBaseClasses/EventManager.h>

#define PROP_INVERT 0x80

class View;
struct AppWindowViews;

class AppWindow : public GUIWindow, I_Observer, Status {
protected:
  AppWindow(I_GUIWindowImp &imp, const char *projectName);
  virtual ~AppWindow();

public:
  static AppWindow *Create(GUICreateWindowParams &, const char *projectName);
  static AppWindow *GetInstance();

  static GUIColor GetColor(ColorDefinition cd);

  enum LoadProjectResult { LOAD_FAILED = -1, LOAD_OK = 0 };

  LoadProjectResult LoadProject(const char *name);
  void CloseProject();

  using GUIWindow::Clear;
  virtual void Clear(bool all = false);
  virtual void ClearTextRect(GUIRect &rect);
  virtual void SetColor(ColorDefinition cd);
  void InvalidateTextCache();
  void ConfigureGraphics(GraphicsLayout layout);
  uint32_t GraphicsRevision() const;
  void DrawGraphicsRect(uint8_t region, const GUIRect &local,
                        ColorDefinition color);
  static constexpr uint32_t PowerOverlayOwner = UINT32_MAX;
  bool HasOverlay(uint32_t owner) const;
  void BeginOverlay(uint32_t owner);
  void SetOverlayBounds(GraphicsRegion bounds);
  void EndOverlay();
  void CloseOverlay(uint32_t owner);
  void CloseModalOverlay();
  void DiscardOverlays();
  // Call on screen entry, before submitting text. Pass an explicit definition.
  // Invalid/overlapping layouts are ignored, preserving the current layout.
  bool ConfigureTextRegions(const TextRegion *regions, uint8_t count);
  // Prepared geometry of the installed region; nullptr for an invalid ID.
  const TextRegionInfo *GetTextRegion(uint8_t region) const;
  void DrawRegionString(uint8_t region, const char *text, const GUIPoint &pos,
                        const GUITextProperties &props);
  void ClearRegionTextRect(uint8_t region, const GUIRect &rect);
  void StyleRegionTextRect(uint8_t region, const GUIRect &rect,
                           ColorDefinition color, bool invert);

  void SetDirty();
  void SetMixerViewDirty();
  void PowerDown();
  void UpdateColorsFromConfig();
  void SetSdCardPresent(bool present);
  bool GetSongSelectionRect(GUIRect &selectionRect) const;
  bool
  BuildSongSelectionRenderRequest(SelectionResampleRequest &request,
                                  SelectionOutputIntent outputIntent) const;

  char projectName_[MAX_PROJECT_NAME_LENGTH + 1];
  etl::string<MAX_PROJECT_NAME_LENGTH> originalProjectName_;

protected: // GUIWindow implementation
  virtual bool onEvent(GUIEvent &event);
  virtual void onUpdate(bool redraw);
  virtual void LayoutChildren();
  virtual void Flush();
  virtual void Redraw(){};
  virtual void AnimationUpdate();

  // override draw string to avoid going too far off
  // the screen.
  virtual void DrawString(const char *string, const GUIPoint &pos,
                          const GUITextProperties &props, bool overlay = false);

  // I_Observer implementation

  virtual void Update(Observable &o, I_ObservableData *d);

  // Status implementation

  virtual void Print(char *);
  virtual void PrintMultiLine(char *);

  void defineColor(FourCC colorCode, GUIColor &color, int paletteIndex);

  void onQuitApp();

private:
  // The only application path for entering/restoring a focused screen.
  void FocusView(View &view, ViewType type);
  struct OverlayCache {
    uint32_t owner;
    GraphicsRegion bounds;
    bool placed;
    uint8_t chars[32 * 24], props[32 * 24], previous[32 * 24],
        previousProps[32 * 24];
  };
  // Overlay caches are ordinary static RAM; the hot base cache stays in DTCM.
  static OverlayCache overlays_[GRAPHICS_OVERLAY_LIMIT];
  uint8_t overlayDepth_ = 0, overlayTarget_ = 0;
  bool AutoSave();
  void AutoSaveBeforePowerDown();

  Project project_;
  ViewData viewData_;
  AppWindowViews *views_;
  View *_currentView;

  bool _closeProject;
  bool _shouldQuit;
  unsigned short _mask;
  unsigned long _lastA;
  unsigned long _lastB;
  char _statusLine[80];

  bool lowBatteryState_;
  bool lowBatteryMessageShown_;
  uint16_t lowBatteryWarningCounter_;
  bool sdCardMissing_;
  bool sdCardMessageShown_;

  static unsigned char _charScreen[TEXT_REGION_CAPACITY];
  static unsigned char _charScreenProp[TEXT_REGION_CAPACITY];
  static unsigned char _preScreen[TEXT_REGION_CAPACITY];
  static unsigned char _preScreenProp[TEXT_REGION_CAPACITY];

  static GUIColor backgroundColor_;
  static GUIColor normalColor_;
  static GUIColor highlightColor_;
  static GUIColor highlight2Color_;
  static GUIColor consoleColor_;
  static GUIColor cursorColor_;
  static GUIColor infoColor_;
  static GUIColor warnColor_;
  static GUIColor errorColor_;
  static GUIColor accentColor_;
  static GUIColor accentAltColor_;
  static GUIColor emphasisColor_;
  static GUIColor reserved1Color_;
  static GUIColor reserved2Color_;
  static GUIColor reserved3Color_;
  static GUIColor reserved4Color_;

  ColorDefinition colorIndex_;

  bool loadProject_ = false;
  bool awaitingProjectLoadAck_ = false;
  bool createProjectOnLoad_ = false;
  bool playerInitialized_ = false;

  uint32_t lastAutoSave = 0;

  // Counter for animation frames, updated once per frame at PICO_CLOCK_HZ
  static uint32_t animationFrameCounter_;

public:
  // Static accessor for the animation frame counter
  static uint32_t GetAnimationFrameCounter() { return animationFrameCounter_; }
};

#endif
