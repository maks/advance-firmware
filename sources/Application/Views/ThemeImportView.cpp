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

#include "ThemeImportView.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Views/BaseClasses/BrowserEntrySort.h"
#include "Application/Views/BaseClasses/BrowserPaging.h"
#include "Application/Views/BaseClasses/BrowserPathUtils.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/to_string.h"
#include "ModalDialogs/MessageBox.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include "ThemeView.h"
#include <cstring>

#define LIST_WIDTH (SCREEN_WIDTH - 2)
// -4 to allow for title, filesize & spacers
#define LIST_PAGE_SIZE (SCREEN_HEIGHT - 4)

namespace {

const FileSystemEntry *
getCurrentEntry(const etl::ivector<FileSystemEntry> &entries,
                const size_t currentIndex) {
  if (currentIndex >= entries.size()) {
    return nullptr;
  }
  return &entries[currentIndex];
}

bool resolveEntryPath(const FileSystemPath::PathString &currentDir,
                      const FileSystemEntry &entry,
                      FileSystemPath::PathString &resolvedPath) {
  if (entry.name == BrowserPathUtils::kParentDirectoryName) {
    return BrowserPathUtils::ResolveParentDirectory(currentDir.c_str(),
                                                    THEMES_DIR, resolvedPath);
  }

  return FileSystemPath::JoinPath(currentDir.c_str(), entry.name.c_str(),
                                  resolvedPath);
}

bool isDirectoryListTruncated(FileSystem *fs) {
  return fs != nullptr &&
         fs->GetLastListDirectoryStatus() == DirectoryListStatus::Truncated;
}

void showDirectoryListTruncatedDialog(View &view) {
  etl::string<24> line("Showing first ");
  etl::to_string(MAX_FILE_INDEX_SIZE, line, true);
  MessageBox *mb =
      MessageBox::Create(view, "Directory too large", line.c_str(), MBBF_OK);
  view.DoModal(mb);
}

} // namespace

ThemeImportView::ThemeImportView(GUIWindow &w, ViewData *viewData)
    : ScreenView(w, viewData), fileEntries_(GetThemeBrowserFileEntries()) {}

ThemeImportView::~ThemeImportView() {}

void ThemeImportView::Reset() {
  topIndex_ = 0;
  currentIndex_ = 0;
  currentDir_.clear();
  fileEntries_.clear();
}

void ThemeImportView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed)
    return;

  if (mask & EPBM_PLAY) {
    const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
    FileSystemPath::PathString themePath;
    if (entry != nullptr && (mask & EPBM_ALT) && entry->type == PFT_FILE) {
      if (!resolveEntryPath(currentDir_, *entry, themePath)) {
        return;
      }
      Trace::Log("THEMEIMPORT", "SHIFT play - import");
      onImportTheme(themePath.c_str());
    }

    // handle moving up and down the file list
  } else if (mask & EPBM_UP) {
    const bool page = (mask & EPBM_EDIT) != 0;
    warpToNextTheme(true, page);
  } else if (mask & EPBM_DOWN) {
    const bool page = (mask & EPBM_EDIT) != 0;
    warpToNextTheme(false, page);
  } else if ((mask & EPBM_LEFT) && (mask & EPBM_NAV)) {
    // Go to back "left" to theme screen
    ViewType vt = VT_THEME;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  } else {
    // ENTER modifier
    if (mask & EPBM_ENTER) {
      auto fs = FileSystem::GetInstance();
      const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
      FileSystemPath::PathString selectedPath;
      if (entry == nullptr) {
        return;
      }

      if (!resolveEntryPath(currentDir_, *entry, selectedPath)) {
        return;
      }

      if (entry->type == PFT_DIR) {
        setCurrentFolder(fs, selectedPath.c_str());
        isDirty_ = true;
        topIndex_ = 0;
      } else {
        onImportTheme(selectedPath.c_str());
      }
    }
  }
};

void ThemeImportView::DrawView() {
  Clear();

  GUITextProperties props;

  // Draw title
  setTitle(etl::string<23>("Import Theme"));

  SetColor(CD_NORMAL);

  // Draw theme files
  int x = 1;
  int y = 1;

  for (size_t i = topIndex_;
       i < topIndex_ + LIST_PAGE_SIZE && (i < fileEntries_.size()); i++) {
    if (i == currentIndex_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    }

    const auto &entry = fileEntries_[i];
    if (entry.type == PFT_DIR) {
      if (entry.name == BrowserPathUtils::kParentDirectoryName) {
        DrawRegionString(TEXT_REGION_BODY, x, y,
                         BrowserPathUtils::kParentDirectoryDisplayName, props);
      } else {
        DrawRegionString(TEXT_REGION_BODY, x, y, "[", props);
        DrawRegionString(TEXT_REGION_BODY, x + 1, y, entry.name.c_str(), props);
        DrawRegionString(TEXT_REGION_BODY, x + entry.name.size() + 1, y, "]",
                         props);
      }
    } else {
      char buffer[PFILENAME_SIZE] = {};
      std::strncpy(buffer, entry.name.c_str(), sizeof(buffer) - 1);
      char *dot = strrchr(buffer, '.');
      if (dot) {
        *dot = '\0';
      }
      DrawRegionString(TEXT_REGION_BODY, x, y, buffer, props);
    }
    y++;
  }
};

void ThemeImportView::OnPlayerUpdate(PlayerEventType, unsigned int tick) {}

void ThemeImportView::OnFocus() {
  auto fs = FileSystem::GetInstance();
  setCurrentFolder(fs, THEMES_DIR);
}

void ThemeImportView::warpToNextTheme(bool goUp, bool page) {
  BrowserPaging::Warp(currentIndex_, topIndex_, fileEntries_.size(),
                      LIST_PAGE_SIZE, goUp, page);
  isDirty_ = true;
}

void ThemeImportView::onImportTheme(const char *filename) {
  Config *config = Config::GetInstance();
  bool result = config->ImportTheme(filename);

  if (result) {
    config->Save();

    AppWindow &app = (AppWindow &)w_;
    app.UpdateColorsFromConfig();

    MessageBox *mb =
        MessageBox::Create(*this, "Theme imported successfully", MBBF_OK);
    DoModal(
        mb,
        ModalViewCallback::create<ThemeImportView,
                                  &ThemeImportView::onImportThemeModalDismiss>(
            *this));
  } else {
    MessageBox *mb =
        MessageBox::Create(*this, "Failed to import theme", MBBF_OK);
    DoModal(
        mb,
        ModalViewCallback::create<ThemeImportView,
                                  &ThemeImportView::onImportThemeModalDismiss>(
            *this));
  }
  isDirty_ = true;
}

void ThemeImportView::onImportThemeModalDismiss(View &, ModalView &dialog) {
  if (dialog.GetReturnCode() != MBL_OK) {
    return;
  }

  ViewType vt = VT_THEME;
  ViewEvent ve(VET_SWITCH_VIEW, &vt);
  SetChanged();
  NotifyObservers(&ve);
}

void ThemeImportView::setCurrentFolder(FileSystem *fs, const char *name) {
  if (fs == nullptr || name == nullptr) {
    return;
  }

  FileSystemPath::PathString nextDir;
  if (std::strcmp(name, BrowserPathUtils::kParentDirectoryName) == 0) {
    if (!BrowserPathUtils::ResolveParentDirectory(currentDir_.c_str(),
                                                  THEMES_DIR, nextDir)) {
      return;
    }
  } else if (FileSystemPath::IsAbsolutePath(name)) {
    if (!FileSystemPath::NormalizePath(name, nextDir)) {
      Trace::Error("THEMEIMPORT: Failed to normalize %s", name);
      return;
    }
  } else if (!FileSystemPath::JoinPath(currentDir_.c_str(), name, nextDir)) {
    Trace::Error("THEMEIMPORT: Failed to resolve %s from %s", name,
                 currentDir_.c_str());
    return;
  }

  fileEntries_.clear();
  const bool listed = fs->ListDirectory(nextDir.c_str(), &fileEntries_,
                                        THEME_FILE_EXTENSION, false);
  const bool truncated = isDirectoryListTruncated(fs);
  if (!listed && !truncated) {
    Trace::Error("THEMEIMPORT: Failed to list %s", nextDir.c_str());
    return;
  }
  if (truncated) {
    Trace::Error("THEMEIMPORT: Truncated listing for %s", nextDir.c_str());
    showDirectoryListTruncatedDialog(*this);
  }

  SortBrowserEntries(fileEntries_);

  currentDir_ = nextDir;
  currentIndex_ = 0;
  topIndex_ = 0;
  isDirty_ = true;
  BrowserPathUtils::PrependParentEntry(currentDir_.c_str(), THEMES_DIR,
                                       &fileEntries_);
  Trace::Debug("loaded %d files from %s", fileEntries_.size(),
               currentDir_.c_str());
}
