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

#ifndef _IMPORT_VIEW_H_
#define _IMPORT_VIEW_H_

#include "Application/Views/BaseClasses/BrowserListingStorage.h"
#include "Application/Views/ScreenView.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include "ViewData.h"
#include <string>

class ImportView : public ScreenView {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 704, 690, TEXT_FONT_NORMAL},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  ImportView(GUIWindow &w, ViewData *viewData);
  ~ImportView();
  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();

  // Static method to set which view will open the ImportView
  static void SetSourceViewType(ViewType vt);

  // Track which view opened the ImportView (default to project view)
  static ViewType sourceViewType_;

  // Navigation intent for setCurrentFolder(). Callers declare it explicitly
  // instead of the view guessing from the path:
  //  - DrillDown: enter a subdirectory of the current one; the selection
  //    position of the directory being left is saved on the position stack.
  //  - Up: move to the parent directory; the saved position is restored.
  //  - Jump: open the browser in an unrelated directory and discard history.
  //  - ResumeImport: return from the project pool to the saved import position.
  enum class FolderNav : uint8_t { DrillDown = 0, Up, Jump, ResumeImport };

protected:
  void setCurrentFolder(FileSystem *fs, const char *name, FolderNav nav);
  void warpToNextSample(bool goUp, bool page = false);
  void import();
  void preview();
  void adjustPreviewVolume(int offset);
  void showSampleEditor(etl::string<PFILENAME_SIZE> filename,
                        bool isProjectSample);
  void renameSample(const FileSystemEntry &entry, FileSystem *fs);
  void removeProjectSample(const FileSystemEntry &entry, FileSystem *fs);
  void refreshFileIndexList(FileSystem *fs);
  bool getSelectedSamplePoolSize(uint32_t &sampleSize);
  void invalidateSelectedSamplePoolSize();

private:
  // Max number of drilled-down levels whose selection position is
  // remembered. As the user drills down through subdirectories the cursor
  // position of each parent directory is stacked, and going back up
  // restores it, so navigating deep sample pack hierarchies does not reset
  // the position every level. Trees deeper than the stack simply drop the
  // oldest (shallowest) records; navigation itself is unbounded. The stack
  // is discarded on Jump navigations and in Reset() (power cycle, project
  // load / new project). The project pool leaves this history intact.
  static constexpr size_t maxDirIndexStackDepth = 32;

  void onConfirmRenameSample(View &view, ModalView &dialog);
  void onConfirmRemoveProjectSample(View &view, ModalView &dialog);

  size_t topIndex_ = 0;
  size_t currentIndex_ = 0;
  size_t savedImportTopIndex_ = 0;
  size_t savedImportIndex_ = 0;
  size_t previewPlayingIndex_ = 0;
  short selectedButton_ = 0;
  int toInstr_ = 0;
  bool playKeyHeld_ =
      false; // Flag to track when the play key is being held down
  bool editKeyHeld_ =
      false; // Flag to track when the edit key is being held down
  bool enterKeyHeld_ = false; // Track ENTER key state for deferred dir-enter
  bool pendingDirEnterOnRelease_ = false; // Open dir on ENTER release
  bool inProjectSampleDir_ =
      false; // Flag to track if we're in the project's sample directory
  size_t dirIndexDepth_ = 0;
  uint16_t dirIndexStack_[maxDirIndexStackDepth] = {};
  FileSystemPath::PathString currentDir_;
  char pendingRenamePath_[PFILENAME_SIZE] = {};
  char pendingDeletePath_[PFILENAME_SIZE] = {};
  size_t samplePoolSizeIndex_ = static_cast<size_t>(-1);
  uint32_t selectedSamplePoolSize_ = 0;
  bool selectedSamplePoolSizeValid_ = false;
  BrowserFileEntries &fileEntries_;
};
#endif
