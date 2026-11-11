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

#ifndef _THEME_IMPORT_VIEW_H_
#define _THEME_IMPORT_VIEW_H_

#include "Application/Views/BaseClasses/BrowserListingStorage.h"
#include "ScreenView.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include "ViewData.h"
#include <string>

class ThemeImportView : public ScreenView {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 704, 690, TEXT_FONT_NORMAL},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  ThemeImportView(GUIWindow &w, ViewData *viewData);
  ~ThemeImportView();
  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();

protected:
  void setCurrentFolder(FileSystem *fs, const char *name);
  void warpToNextTheme(bool goUp, bool page = false);
  void onImportTheme(const char *filename);

private:
  void onImportThemeModalDismiss(View &view, ModalView &dialog);

  size_t topIndex_ = 0;
  size_t currentIndex_ = 0;
  FileSystemPath::PathString currentDir_;
  BrowserFileEntries &fileEntries_;
};
#endif
