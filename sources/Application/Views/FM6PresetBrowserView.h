/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _FM6_PRESET_BROWSER_VIEW_H_
#define _FM6_PRESET_BROWSER_VIEW_H_

#include "ScreenView.h"
#include "ViewData.h"
#include <cstddef>
#include <cstdint>

// Two-column browser for the built-in FM6 (DX7) presets: banks in the left
// column, the selected bank's patches in the right. An [SDCard] row at the top
// of the bank column hands off to the DX7 .syx file importer
// (InstrumentImportView in Dx7Patch mode), which returns here when backed out
// at the filesystem root.
class FM6PresetBrowserView : public ScreenView {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 704, 690, TEXT_FONT_NORMAL},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  FM6PresetBrowserView(GUIWindow &w, ViewData *viewData);
  ~FM6PresetBrowserView();
  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();

private:
  void commitBuiltInPatch(size_t presetIndex);
  void startPatchPreview(const uint8_t *record);
  void stopPatchPreview();
  void openSysexFileBrowser();
  void exitToInstrumentView();

  int toInstrID_ = 0;
  // Cursor state. Left-column rows are: 0 = [SDCard], 1..bankCount = built-in
  // banks. patchIndex_ selects a patch within the current bank and patchTop_
  // scrolls the right column; patchFocus_ is true when the patch column (right)
  // holds the cursor instead of the bank column.
  size_t bankRow_ = 1;
  size_t patchIndex_ = 0;
  size_t patchTop_ = 0;
  bool patchFocus_ = false;
  bool previewKeyHeld_ = false;
  unsigned short previewChannel_ = 0;
};
#endif
