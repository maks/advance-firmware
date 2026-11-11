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

#ifndef _INSTRUMENT_IMPORT_VIEW_H_
#define _INSTRUMENT_IMPORT_VIEW_H_

#include "Application/Views/BaseClasses/BrowserListingStorage.h"
#include "ScreenView.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include "ViewData.h"
#include <cstdint>
#include <string>

class InstrumentImportView : public ScreenView {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 704, 690, TEXT_FONT_NORMAL},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  // Dx7Patch browses SD-card .syx bank files. It is entered from the FM6
  // preset browser's [SDCard] entry, and ".." at the filesystem root returns
  // there.
  enum class Mode : uint8_t { Instrument, Dx7Patch };

  InstrumentImportView(GUIWindow &w, ViewData *viewData);
  ~InstrumentImportView();
  static void SetMode(Mode mode);
  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();

protected:
  void setCurrentFolder(FileSystem *fs, const char *name);
  void warpToNextInstrument(bool goUp, bool page = false);
  void importInstrument(const char *name);
  // Opens a validated .syx bank as a virtual directory of its 32 patches.
  void enterSysexFile(const char *path);
  void leaveSysexListing();
  void commitSysexPatch(size_t patchIndex);
  void startPatchPreview(const uint8_t *record);
  void stopPatchPreview();

private:
  void onImportSuccess(View &view, ModalView &dialog);

  static Mode mode_;

  size_t topIndex_ = 0;
  size_t currentIndex_ = 0;
  short selected_ = 0;
  int toInstrID_ = 0;
  // When set, fileEntries_ lists the 32 patches of the staged sysex bank
  // instead of the filesystem.
  bool browsingSysex_ = false;
  bool previewKeyHeld_ = false;
  unsigned short previewChannel_ = 0;
  FileSystemPath::PathString currentDir_;
  BrowserFileEntries &fileEntries_;
};
#endif
