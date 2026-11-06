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

#include "ImportView.h"
#include "Application/Audio/AudioFileStreamer.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Instruments/WavHeader.h"
#include "Application/Views/BaseClasses/BrowserEntrySort.h"
#include "Application/Views/BaseClasses/BrowserPaging.h"
#include "Application/Views/BaseClasses/BrowserPathUtils.h"
#include "Application/Views/ModalDialogs/TextInputModalView.h"
#include "Application/Views/SampleEditorView.h"
#include "Application/Views/SampleImportUtils.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/to_string.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "ModalDialogs/MessageBox.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include <cstring>
#include <memory>
#include <nanoprintf.h>
#include <strings.h>

#define LIST_WIDTH SCREEN_WIDTH - 2
// -4 to allow for title, filesize & spacers
#define LIST_PAGE_SIZE SCREEN_HEIGHT - 5

// Initialize static member
ViewType ImportView::sourceViewType_ = VT_PROJECT;

namespace {
enum ImportButton : uint8_t {
  kImportButtonImport = 0,
  kImportButtonEdit,
  kImportButtonRename,
  kImportButtonVolume,
  kImportButtonCount,
};

enum ProjectPoolButton : uint8_t {
  kProjectButtonEdit = 0,
  kProjectButtonRename,
  kProjectButtonRemove,
  kProjectButtonVolume,
  kProjectPoolButtonCount,
};

template <size_t Capacity>
etl::string<Capacity> makeBoundedSampleName(const char *name) {
  etl::string<Capacity> boundedName;
  if (name == nullptr) {
    return boundedName;
  }

  const size_t nameLength = std::strlen(name);
  if (nameLength <= boundedName.capacity()) {
    boundedName = name;
    return boundedName;
  }

  constexpr const char *extension = ".wav";
  constexpr size_t extensionLength = 4;
  if (nameLength > extensionLength &&
      strcasecmp(name + nameLength - extensionLength, extension) == 0 &&
      boundedName.capacity() > extensionLength) {
    boundedName.assign(name, boundedName.capacity() - extensionLength);
    boundedName.append(extension);
    return boundedName;
  }

  boundedName.assign(name, boundedName.capacity());
  return boundedName;
}

bool extractLeafName(const char *path,
                     etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &leafName) {
  FileSystemPath::PathString baseName;
  if (!FileSystemPath::BaseName(path, baseName)) {
    leafName.clear();
    return false;
  }

  leafName =
      makeBoundedSampleName<MAX_INSTRUMENT_FILENAME_LENGTH>(baseName.c_str());
  return !leafName.empty();
}

const FileSystemEntry *
getCurrentEntry(const etl::ivector<FileSystemEntry> &entries,
                const size_t currentIndex) {
  if (currentIndex >= entries.size()) {
    return nullptr;
  }
  return &entries[currentIndex];
}

bool resolveEntryPath(const FileSystemPath::PathString &currentDir,
                      const FileSystemEntry &entry, const char *rootDir,
                      FileSystemPath::PathString &resolvedPath) {
  if (entry.name == BrowserPathUtils::kParentDirectoryName) {
    return BrowserPathUtils::ResolveParentDirectory(currentDir.c_str(), rootDir,
                                                    resolvedPath);
  }

  return FileSystemPath::JoinPath(currentDir.c_str(), entry.name.c_str(),
                                  resolvedPath);
}

bool isDirectoryListTruncated(FileSystem *fs) {
  return fs != nullptr &&
         fs->GetLastListDirectoryStatus() == DirectoryListStatus::Truncated;
}

etl::string<PFILENAME_SIZE>
truncateMiddle(const etl::string<PFILENAME_SIZE> &name, const size_t maxWidth) {
  constexpr const char *marker = "...";
  constexpr size_t markerWidth = 3;

  if (name.size() <= maxWidth) {
    return name;
  }
  if (maxWidth <= markerWidth) {
    return name.substr(0, maxWidth);
  }

  const size_t visibleNameWidth = maxWidth - markerWidth;
  const size_t prefixWidth = (visibleNameWidth + 1) / 2;
  const size_t suffixWidth = visibleNameWidth - prefixWidth;
  etl::string<PFILENAME_SIZE> truncated = name.substr(0, prefixWidth);
  truncated.append(marker);
  truncated.append(name.substr(name.size() - suffixWidth).c_str());
  return truncated;
}

void showDirectoryListTruncatedDialog(View &view) {
  etl::string<24> line("Showing first ");
  etl::to_string(MAX_FILE_INDEX_SIZE, line, true);
  MessageBox *mb =
      MessageBox::Create(view, "Directory too large", line.c_str(), MBBF_OK);
  view.DoModal(mb);
}

bool hasPathSeparator(const char *name) {
  return name != nullptr && (std::strchr(name, '/') != nullptr ||
                             std::strchr(name, '\\') != nullptr);
}

// Strip the ".wav" extension from a filename (case-insensitive).
// Returns the name without the extension so the user only edits the basename.
template <size_t Capacity>
etl::string<Capacity> stripWavExtension(const char *name) {
  etl::string<Capacity> result;
  if (name == nullptr) {
    return result;
  }

  constexpr const char *extension = ".wav";
  constexpr size_t extensionLength = 4;
  const size_t nameLength = std::strlen(name);

  if (nameLength > extensionLength &&
      strcasecmp(name + nameLength - extensionLength, extension) == 0) {
    result.assign(name, nameLength - extensionLength);
    return result;
  }

  result = name;
  return result;
}

bool ensureWavExtension(etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &name) {
  constexpr const char *extension = ".wav";
  constexpr size_t extensionLength = 4;
  if (name.size() >= extensionLength &&
      strcasecmp(name.c_str() + name.size() - extensionLength, extension) ==
          0) {
    return true;
  }
  if (name.size() + extensionLength > name.capacity()) {
    return false;
  }
  name.append(extension);
  return true;
}

} // namespace

ImportView::ImportView(GUIWindow &w, ViewData *viewData)
    : ScreenView(w, viewData), fileEntries_(GetImportBrowserFileEntries()) {}

ImportView::~ImportView() {}

void ImportView::Reset() {
  topIndex_ = 0;
  currentIndex_ = 0;
  savedImportTopIndex_ = 0;
  savedImportIndex_ = 0;
  previewPlayingIndex_ = 0;
  selectedButton_ = 0;
  toInstr_ = 0;
  playKeyHeld_ = false;
  editKeyHeld_ = false;
  enterKeyHeld_ = false;
  pendingDirEnterOnRelease_ = false;
  inProjectSampleDir_ = false;
  dirIndexDepth_ = 0;
  std::memset(dirIndexStack_, 0, sizeof(dirIndexStack_));
  currentDir_.clear();
  pendingRenamePath_[0] = '\0';
  invalidateSelectedSamplePoolSize();
  fileEntries_.clear();
}

// Static method to set the source view type before opening ImportView
void ImportView::SetSourceViewType(ViewType vt) { sourceViewType_ = vt; }

void ImportView::ProcessButtonMask(unsigned short mask, bool pressed) {
  // Check for key release events
  if (!pressed) {
    // Open selected directory only when ENTER is released, unless ENTER was
    // consumed by another action (e.g. volume edit).
    if (enterKeyHeld_ && !(mask & EPBM_ENTER)) {
      enterKeyHeld_ = false;
      if (pendingDirEnterOnRelease_ && !fileEntries_.empty()) {
        auto fs = FileSystem::GetInstance();
        const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
        FileSystemPath::PathString selectedPath;
        if (entry != nullptr && entry->type == PFT_DIR) {
          if (entry->name == BrowserPathUtils::kParentDirectoryName) {
            // The synthetic parent entry navigates via the ".." sentinel so
            // the parent's saved selection position gets restored.
            setCurrentFolder(fs, BrowserPathUtils::kParentDirectoryName,
                             ImportView::FolderNav::Up);
          } else if (resolveEntryPath(currentDir_, *entry, "/", selectedPath)) {
            setCurrentFolder(fs, selectedPath.c_str(),
                             ImportView::FolderNav::DrillDown);
          }
          isDirty_ = true;
          topIndex_ = 0;
        }
      }
      pendingDirEnterOnRelease_ = false;
      return;
    }

    // Check if play key was released
    if (playKeyHeld_ && !(mask & EPBM_PLAY)) {
      // Play key no longer pressed so should stop preview playback.
      // Only stop the file streamer, not sequencer channels.
      playKeyHeld_ = false;
      if (Player::GetInstance()->IsStreamingActive()) {
        Player::GetInstance()->StopStreaming();
        previewPlayingIndex_ = (size_t)-1;
      }
      return;
    }

    // Check if edit key was released
    if (editKeyHeld_ && !(mask & EPBM_EDIT)) {
      // Edit key no longer pressed, redraw the view to clear status message
      editKeyHeld_ = false;
      isDirty_ = true; // Mark view as dirty to force redraw
      return;
    }
  }

  // Handle key press events
  if (pressed) {
    auto fs = FileSystem::GetInstance();
    const bool hasFiles = !fileEntries_.empty();

    // NAV+UP: go to parent directory within the import file browser.
    // Handle this before ordinary UP navigation so the selection does not move.
    if ((mask & EPBM_NAV) && (mask & EPBM_UP) && !inProjectSampleDir_) {
      setCurrentFolder(fs, BrowserPathUtils::kParentDirectoryName,
                       ImportView::FolderNav::Up);
      isDirty_ = true;
      topIndex_ = 0;
      return;
    }

    if (mask & EPBM_EDIT) {
      // Set flag to track that edit key is being held
      editKeyHeld_ = true;
    }

    if (mask & EPBM_PLAY) {
      if (!hasFiles) {
        return;
      }
      const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
      if (entry == nullptr) {
        return;
      }

      // Set flag to track that play key is being held
      playKeyHeld_ = true;

      if (mask & EPBM_ALT) {
        Trace::Log("PICOIMPORT", "SHIFT play - import");
        import();
      } else {
        Trace::Log("PICOIMPORT", "play key pressed - start preview");
        preview();
      }
      return; // We've handled the play button, so return
    }

    if (mask & EPBM_NAV && mask & EPBM_EDIT) {
      // toggle from sdcard "import sample" & project pool listing
      if (inProjectSampleDir_) {
        inProjectSampleDir_ = false;
        const char *importDir = viewData_->importViewStartDir.empty()
                                    ? SAMPLES_LIB_DIR
                                    : viewData_->importViewStartDir.c_str();
        setCurrentFolder(fs, importDir, ImportView::FolderNav::ResumeImport);
      } else {
        FileSystemPath::PathString projectSamplesDir;
        char projectName[MAX_PROJECT_NAME_LENGTH + 1] = {};
        viewData_->project_->GetProjectName(projectName);
        if (!BrowserPathUtils::BuildProjectSamplesDirectory(
                projectName, projectSamplesDir)) {
          Trace::Error("PICOIMPORT: Failed to build project samples dir");
          return;
        }
        savedImportIndex_ = currentIndex_;
        savedImportTopIndex_ = topIndex_;
        inProjectSampleDir_ = true;
        setCurrentFolder(fs, projectSamplesDir.c_str(),
                         ImportView::FolderNav::Jump);
      }
      selectedButton_ = 0;
    }

    if (mask & EPBM_ENTER) {
      if (!enterKeyHeld_) {
        enterKeyHeld_ = true;
        pendingDirEnterOnRelease_ = true;
      }
      if (mask & (EPBM_LEFT | EPBM_RIGHT | EPBM_UP | EPBM_DOWN)) {
        pendingDirEnterOnRelease_ = false;
      }

      if (inProjectSampleDir_) {
        if (selectedButton_ == kProjectButtonVolume) {
          int volumeOffset = 0;
          if (mask & EPBM_LEFT) {
            volumeOffset -= 1;
          }
          if (mask & EPBM_RIGHT) {
            volumeOffset += 1;
          }
          if (mask & EPBM_DOWN) {
            volumeOffset -= 5;
          }
          if (mask & EPBM_UP) {
            volumeOffset += 5;
          }
          if (volumeOffset != 0) {
            adjustPreviewVolume(volumeOffset);
          }
          return;
        }
        if (!hasFiles) {
          pendingDirEnterOnRelease_ = false;
          return; // Do nothing if the list is empty
        }
        if (selectedButton_ == kProjectButtonEdit) {
          const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
          FileSystemPath::PathString samplePath;
          if (entry == nullptr ||
              !resolveEntryPath(currentDir_, *entry, currentDir_.c_str(),
                                samplePath)) {
            return;
          }
          showSampleEditor(samplePath, true);
        } else if (selectedButton_ == kProjectButtonRename) {
          const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
          if (entry == nullptr) {
            return;
          }
          renameSample(*entry, fs);
        } else if (selectedButton_ == kProjectButtonRemove) {
          const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
          FileSystemPath::PathString samplePath;
          if (entry == nullptr ||
              !resolveEntryPath(currentDir_, *entry, currentDir_.c_str(),
                                samplePath)) {
            return;
          }
          removeProjectSample(*entry, fs);
        }
        return;
      }
      if (selectedButton_ == kImportButtonVolume) {
        int volumeOffset = 0;
        if (mask & EPBM_LEFT) {
          volumeOffset -= 1;
        }
        if (mask & EPBM_RIGHT) {
          volumeOffset += 1;
        }
        if (mask & EPBM_DOWN) {
          volumeOffset -= 5;
        }
        if (mask & EPBM_UP) {
          volumeOffset += 5;
        }
        if (volumeOffset != 0) {
          adjustPreviewVolume(volumeOffset);
        }
        return;
      }
      if (!hasFiles) {
        pendingDirEnterOnRelease_ = false;
        return;
      }
      const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
      if (entry == nullptr) {
        pendingDirEnterOnRelease_ = false;
        return;
      }
      // we can't import or edit dirs!
      if (entry->type != PFT_DIR) {
        if (selectedButton_ == kImportButtonImport) {
          import();
        } else if (selectedButton_ == kImportButtonEdit) {
          FileSystemPath::PathString samplePath;
          if (!resolveEntryPath(currentDir_, *entry, "/", samplePath)) {
            return;
          }
          showSampleEditor(samplePath, false);
        } else if (selectedButton_ == kImportButtonRename) {
          renameSample(*entry, fs);
        }
      }
    }

    // handle changing selected "bottom button", note: ignore if this is a
    // nav+arrow combo
    if ((mask & EPBM_LEFT || mask & EPBM_RIGHT) && !(mask & EPBM_NAV)) {
      if (inProjectSampleDir_ && fileEntries_.empty()) {
        return; // Do nothing if the list is empty
      }
      uint8_t buttonCount = inProjectSampleDir_
                                ? static_cast<uint8_t>(kProjectPoolButtonCount)
                                : static_cast<uint8_t>(kImportButtonCount);
      if (mask & EPBM_LEFT) {
        selectedButton_ = (selectedButton_ + buttonCount - 1) % buttonCount;
      } else {
        selectedButton_ = (selectedButton_ + 1) % buttonCount;
      }
      DrawView();
    }
  }

  // Only process other buttons when pressed
  if (!pressed)
    return;

  // handle moving up and down the file list
  if (mask & EPBM_UP) {
    if (inProjectSampleDir_ && fileEntries_.empty()) {
      return; // Do nothing if the list is empty
    }
    const bool page = (mask & EPBM_EDIT) != 0;
    warpToNextSample(true, page);
  } else if (mask & EPBM_DOWN) {
    if (inProjectSampleDir_ && fileEntries_.empty()) {
      return; // Do nothing if the list is empty
    }
    const bool page = (mask & EPBM_EDIT) != 0;
    warpToNextSample(false, page);
  } else if ((mask & EPBM_LEFT) && (mask & EPBM_NAV)) {
    // clear this flag on leaving this screen
    viewData_->isShowingSampleEditorProjectPool = false;

    // Go back to the source view that opened the ImportView
    ViewEvent ve(VET_SWITCH_VIEW, &sourceViewType_);
    SetChanged();
    NotifyObservers(&ve);
    return;
  } else {
    // A modifier
  }
};

void ImportView::DrawView() {
  // ensure selected item is in visible range
  const size_t pageSize = LIST_PAGE_SIZE;
  if (!fileEntries_.empty()) {
    if (currentIndex_ < topIndex_) {
      topIndex_ = currentIndex_;
    } else if (currentIndex_ >= topIndex_ + pageSize) {
      topIndex_ = currentIndex_ - pageSize + 1;
    }
  }

  Clear();

  GUITextProperties props;

  auto fs = FileSystem::GetInstance();

  // Draw title with available storage space
  const char *baseTitle =
      inProjectSampleDir_
          ? "Pool  NAV+EDIT" char_indicator_rightNoLeft_s "SDCard"
          : "Import  NAV+EDIT" char_indicator_rightNoLeft_s "Pool";

  setTitle(etl::string<23>(baseTitle));

  // Draw samples
  int x = 1;
  int y = 1;

  uint32_t availableSpace =
      SamplePool::GetInstance()->GetAvailableSampleStorageSpace();

  // Loop through visible files in the list
  for (size_t i = topIndex_;
       i < topIndex_ + LIST_PAGE_SIZE && (i < fileEntries_.size()); i++) {
    props.invert_ = false;

    const auto &entry = fileEntries_[i];
    etl::string<PFILENAME_SIZE> displayName;

    if (entry.type != PFT_DIR) {
      SetColor(CD_NORMAL);

      // Check if it's a single cycle waveform
      const uint64_t filesize = entry.size;
      bool isSingleCycle = IsSingleCycleWaveformFile(filesize);

      displayName += entry.name.c_str();
      // Format the display name with appropriate prefix
      if (inProjectSampleDir_ &&
          viewData_->project_->SampleInUse(
              makeBoundedSampleName<MAX_INSTRUMENT_FILENAME_LENGTH>(
                  entry.name.c_str())) > -1) {
        SetColor(CD_ACCENT);
        DrawRegionString(TEXT_REGION_BODY, x, y, "*", props);
        SetColor(CD_NORMAL);
      } else if (isSingleCycle) {
        SetColor(CD_ACCENT);
        DrawRegionString(TEXT_REGION_BODY, x, y, "~", props);
        SetColor(CD_NORMAL);
      } else {
        DrawRegionString(TEXT_REGION_BODY, x, y, " ", props);
      }

      constexpr size_t wavExtensionLength = 4;
      if (displayName.size() >= wavExtensionLength &&
          strcasecmp(displayName.c_str() + displayName.size() -
                         wavExtensionLength,
                     ".wav") == 0) {
        displayName.resize(displayName.size() - wavExtensionLength);
      }
    } else {
      SetColor(CD_ACCENT);
      // Handle directories
      if (entry.name == BrowserPathUtils::kParentDirectoryName) {
        displayName = BrowserPathUtils::kParentDirectoryDisplayName;
      } else {
        displayName = "/";
        displayName += entry.name.c_str();
      }
    }

    // Keep both ends of long filenames visible in the sample list.
    if (entry.type != PFT_DIR) {
      displayName = truncateMiddle(displayName, LIST_WIDTH);
    } else if (displayName.size() > LIST_WIDTH) {
      displayName.resize(LIST_WIDTH);
    }

    if (i == currentIndex_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    }
    DrawRegionString(TEXT_REGION_BODY, x + 1, y, displayName.c_str(), props);
    y += 1;
  };

  SetColor(CD_HILITE1);
  y = SCREEN_HEIGHT - 3;
  if (inProjectSampleDir_) {
    if (selectedButton_ < 0 || selectedButton_ >= kProjectPoolButtonCount) {
      selectedButton_ = 0;
    }
  } else if (selectedButton_ < 0 || selectedButton_ >= kImportButtonCount) {
    selectedButton_ = 0;
  }

  int previewVolume = 0;
  Variable *v = viewData_->project_->FindVariable(FourCC::VarPreviewVolume);
  if (v) {
    previewVolume = v->GetInt();
  }

  if (!inProjectSampleDir_) {
    if (selectedButton_ == kImportButtonImport) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_HILITE1);
      props.invert_ = false;
    }
    DrawRegionString(TEXT_REGION_BODY, x, y, "Import", props);
    if (selectedButton_ == kImportButtonEdit) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_HILITE1);
      props.invert_ = false;
    }
    DrawRegionString(TEXT_REGION_BODY, x + 8, y, "Edit", props);

    if (selectedButton_ == kImportButtonRename) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_HILITE1);
      props.invert_ = false;
    }
    DrawRegionString(TEXT_REGION_BODY, x + 16, y, "Rename", props);

    if (selectedButton_ == kImportButtonVolume) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_HILITE1);
      props.invert_ = false;
    }
    char volField[12];
    npf_snprintf(volField, sizeof(volField), "Vol:%2d", previewVolume);
    DrawRegionString(TEXT_REGION_BODY, x + 24, y, volField, props);
  } else {
    if (fileEntries_.empty()) {
      // draw this a few lines down from *top* of screen
      SetColor(CD_NORMAL);
      props.invert_ = false;
      DrawRegionString(TEXT_REGION_BODY, 2, 2, "[pool empty]", props);
    } else {
      // we make edit the first button to make things easier because remove is
      // only available for now on the Advance and even on Advance we dont want
      // remove to be the default button
      if (selectedButton_ == 0) {
        SetColor(CD_HILITE2);
        props.invert_ = true;
      } else {
        SetColor(CD_HILITE1);
        props.invert_ = false;
      }
      DrawRegionString(TEXT_REGION_BODY, x, y, "Edit", props);
      if (selectedButton_ == 1) {
        SetColor(CD_HILITE2);
        props.invert_ = true;
      } else {
        SetColor(CD_HILITE1);
        props.invert_ = false;
      }
      DrawRegionString(TEXT_REGION_BODY, x + 7, y, "Rename", props);

      if (selectedButton_ == kProjectButtonRemove) {
        SetColor(CD_HILITE2);
        props.invert_ = true;
      } else {
        SetColor(CD_HILITE1);
        props.invert_ = false;
      }
      DrawRegionString(TEXT_REGION_BODY, x + 16, y, "Remove", props);

      if (selectedButton_ == kProjectButtonVolume) {
        SetColor(CD_HILITE2);
        props.invert_ = true;
      } else {
        SetColor(CD_HILITE1);
        props.invert_ = false;
      }
      char volField[12];
      npf_snprintf(volField, sizeof(volField), "Vol:%2d", previewVolume);
      DrawRegionString(TEXT_REGION_BODY, x + 24, y, volField, props);
    }
  }
  props.invert_ = false;
  y += 1;

  // draw current selected file size and available storage indicator
  SetColor(CD_NORMAL);
  props.invert_ = true;
  y = 0;
  uint32_t samplePoolSize = 0;
  if (!fileEntries_.empty()) {
    const auto &currentEntry = fileEntries_[currentIndex_];
    // Only calculate sample memory usage for a file, not a directory.
    if (currentEntry.type == PFT_FILE &&
        getSelectedSamplePoolSize(samplePoolSize)) {
      // Project-pool samples are already loaded. In the import browser, warn
      // when the converted sample cannot fit entirely in either memory bank.
      if (!inProjectSampleDir_ &&
          !SamplePool::GetInstance()->CheckSampleFits(samplePoolSize)) {
        SetColor(CD_WARN);
      }
    }
  }

  // Create a temporary buffer for formatting
  char tempBuffer[SCREEN_WIDTH];
  tempBuffer[SCREEN_WIDTH - 1] = '\0';

  npf_snprintf(tempBuffer, sizeof(tempBuffer), "Size:%u/%u", samplePoolSize,
               availableSpace);

  // pad status line buffer with trailing space chars to ensure the invert
  // color is applied to entire line
  int32_t padWidth =
      (SCREEN_WIDTH - 2) - static_cast<int32_t>(strlen(tempBuffer));
  if (padWidth < 0) {
    padWidth = 0;
  }
  npf_snprintf(tempBuffer, sizeof(tempBuffer), "%s%*s", tempBuffer, padWidth,
               " ");

  x = 1;                 // align with rest screen title & file list
  y = SCREEN_HEIGHT - 2; // last row in the body text region
  DrawRegionString(TEXT_REGION_BODY, x, y, tempBuffer, props);

  SetColor(CD_NORMAL);
};

void ImportView::OnPlayerUpdate(PlayerEventType, unsigned int tick){};

void ImportView::OnFocus() {
  // clear stale flags
  enterKeyHeld_ = false;
  pendingDirEnterOnRelease_ = false;
  editKeyHeld_ = false;
  playKeyHeld_ = false;

  auto fs = FileSystem::GetInstance();

  toInstr_ = viewData_->currentInstrumentID_;

  const bool enteringProjectPool =
      viewData_->isShowingSampleEditorProjectPool && !inProjectSampleDir_;
  if (enteringProjectPool) {
    savedImportIndex_ = currentIndex_;
    savedImportTopIndex_ = topIndex_;
  }

  inProjectSampleDir_ = viewData_->isShowingSampleEditorProjectPool;

  if (inProjectSampleDir_) {
    FileSystemPath::PathString projectSamplesDir;
    char projectName[MAX_PROJECT_NAME_LENGTH + 1] = {};
    viewData_->project_->GetProjectName(projectName);
    if (!BrowserPathUtils::BuildProjectSamplesDirectory(projectName,
                                                        projectSamplesDir)) {
      Trace::Error("PICOIMPORT: Failed to build project samples dir");
      return;
    }
    setCurrentFolder(fs, projectSamplesDir.c_str(),
                     ImportView::FolderNav::Jump);
  } else {
    const char *startDir = viewData_->importViewStartDir.empty()
                               ? SAMPLES_LIB_DIR
                               : viewData_->importViewStartDir.c_str();
    setCurrentFolder(fs, startDir, ImportView::FolderNav::Jump);
  }
};

void ImportView::warpToNextSample(bool goUp, bool page) {
  BrowserPaging::Warp(currentIndex_, topIndex_, fileEntries_.size(),
                      LIST_PAGE_SIZE, goUp, page);
  isDirty_ = true;
}

bool ImportView::getSelectedSamplePoolSize(uint32_t &sampleSize) {
  if (samplePoolSizeIndex_ == currentIndex_) {
    sampleSize = selectedSamplePoolSize_;
    return selectedSamplePoolSizeValid_;
  }

  samplePoolSizeIndex_ = currentIndex_;
  selectedSamplePoolSize_ = 0;
  selectedSamplePoolSizeValid_ = false;

  const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
  if (entry == nullptr || entry->type != PFT_FILE) {
    return false;
  }

  FileSystemPath::PathString samplePath;
  if (!resolveEntryPath(currentDir_, *entry, "/", samplePath)) {
    return false;
  }

  WavFile wav;
  const auto wavResult = wav.Open(samplePath.c_str());
  if (!wavResult) {
    return false;
  }

  const int32_t sourceFrameCount = wav.GetSize(-1);
  const int32_t sourceSampleRate = wav.GetSampleRate(-1);
  const int32_t sourceChannelCount = wav.GetChannelCount(-1);
  if (sourceFrameCount > 0 && sourceSampleRate > 0 && sourceChannelCount > 0) {
    selectedSamplePoolSizeValid_ = SamplePool::CalculateImportSampleSize(
        static_cast<uint32_t>(sourceFrameCount),
        static_cast<uint32_t>(sourceSampleRate),
        static_cast<uint16_t>(sourceChannelCount), selectedSamplePoolSize_);
  }
  wav.Close();

  sampleSize = selectedSamplePoolSize_;
  return selectedSamplePoolSizeValid_;
}

void ImportView::invalidateSelectedSamplePoolSize() {
  samplePoolSizeIndex_ = static_cast<size_t>(-1);
  selectedSamplePoolSize_ = 0;
  selectedSamplePoolSizeValid_ = false;
}

void ImportView::preview() {
  const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
  if (entry == nullptr || entry->type == PFT_DIR) {
    return;
  }

  FileSystemPath::PathString samplePath;
  if (!resolveEntryPath(currentDir_, *entry, "/", samplePath)) {
    return;
  }

  // Get file size to check if it's a single cycle waveform
  const int32_t fileSize = static_cast<int32_t>(entry->size);

  // check for LGPT or AKWF standard file sizes
  bool isSingleCycle = IsSingleCycleWaveformFile(fileSize);

  // If a preview is already playing, stop it first.
  // Only stop the file streamer, not sequencer channels.
  if (Player::GetInstance()->IsStreamingActive()) {
    Player::GetInstance()->StopStreaming();
  }

  WavFile wav;
  auto wavRes = wav.Open(samplePath.c_str());
  MessageBox *mb = nullptr;
  if (!wavRes) {
    auto error = wavRes.error();
    switch (error) {
    case INVALID_FILE:
      mb = MessageBox::Create(*this, "Preview Failed", "Could not open file",
                              MBBF_OK);
      break;
    case UNSUPPORTED_FILE_FORMAT:
    case INVALID_HEADER:
    case UNSUPPORTED_WAV_FORMAT:
      mb = MessageBox::Create(*this, "Preview Failed", "Invalid file", MBBF_OK);
      break;
    case UNSUPPORTED_AUDIO_FORMAT:
    case UNSUPPORTED_BITDEPTH:
    case UNSUPPORTED_SAMPLERATE:
      mb = MessageBox::Create(*this, "Preview Failed", "Unsupported format",
                              MBBF_OK);
      break;
    }
  } else {
    wav.Close();
  }

  if (mb != nullptr) {
    DoModal(mb);
    return;
  }

  // Start playing the selected sample
  Trace::Debug("Starting preview of %s (single cycle: %d)", samplePath.c_str(),
               isSingleCycle);
  previewPlayingIndex_ = currentIndex_;

  // Use looping for single cycle waveforms
  if (isSingleCycle) {
    Trace::Debug("Looping single cycle waveform: %s (size: %d bytes)",
                 samplePath.c_str(), fileSize);
    Player::GetInstance()->StartLoopingStreaming(samplePath.c_str());
  } else {
    Player::GetInstance()->StartStreaming(samplePath.c_str());
  }
}

void ImportView::import() {
  // Stop any active preview/streamer so we can claim the file. The sequencer
  // may keep running — the new import path is audio-thread safe.
  Player::GetInstance()->StopStreaming();

  auto fs = FileSystem::GetInstance();
  const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
  if (entry == nullptr || entry->type != PFT_FILE) {
    return;
  }

  const bool isSingleCycle = IsSingleCycleWaveformFile(entry->size);

  FileSystemPath::PathString samplePath;
  if (!resolveEntryPath(currentDir_, *entry, "/", samplePath)) {
    return;
  }

  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> sampleFilename;
  if (!extractLeafName(samplePath.c_str(), sampleFilename)) {
    Trace::Error("PICOIMPORT: Failed to extract leaf name from %s",
                 samplePath.c_str());
    return;
  }

  // Get current project name
  char projName[MAX_PROJECT_NAME_LENGTH + 1];
  viewData_->project_->GetProjectName(projName);

  // Check if we're in the project's sample directory
  if (inProjectSampleDir_) {
    MessageBox *mb =
        MessageBox::Create(*this, "Can't import from project!", MBBF_OK);
    DoModal(mb);
    return;
  }

  SamplePool *pool = SamplePool::GetInstance();

  // Check if we've reached the maximum number of samples
  int currentCount = pool->GetNameListSize();
  if (currentCount >= MAX_SAMPLES) {
    // Show error dialog to inform the user
    char message[SCREEN_WIDTH];
    npf_snprintf(message, sizeof(message), "Maximum of %d samples reached",
                 MAX_SAMPLES);
    // pad with trailing spaces as dialog size based on title
    MessageBox *mb = MessageBox::Create(*this, "Cannot Import Sample      ",
                                        message, MBBF_OK);
    DoModal(mb);
    return;
  }

  // Check if wave file is in a supported format
  WavFile wav;
  auto wavRes = wav.Open(samplePath.c_str());
  MessageBox *mb = nullptr;
  if (!wavRes) {
    auto error = wavRes.error();
    switch (error) {
    case INVALID_FILE:
      mb = MessageBox::Create(*this, "Import Failed", "Could not open file",
                              MBBF_OK);
      break;
    case UNSUPPORTED_FILE_FORMAT:
    case INVALID_HEADER:
    case UNSUPPORTED_WAV_FORMAT:
      mb = MessageBox::Create(*this, "Import Failed", "invalid file", MBBF_OK);
      break;
    case UNSUPPORTED_AUDIO_FORMAT:
    case UNSUPPORTED_BITDEPTH:
    case UNSUPPORTED_SAMPLERATE:
      mb = MessageBox::Create(*this, "Import Failed", "unsupported format",
                              MBBF_OK);
      break;
    }
  } else {
    const int32_t sourceFrameCount = wav.GetSize(-1);
    const int32_t sourceSampleRate = wav.GetSampleRate(-1);
    const int32_t sourceChannelCount = wav.GetChannelCount(-1);
    uint32_t convertedSampleSize = 0;
    const bool hasValidConvertedSize =
        sourceFrameCount > 0 && sourceSampleRate > 0 &&
        sourceChannelCount > 0 &&
        SamplePool::CalculateImportSampleSize(
            static_cast<uint32_t>(sourceFrameCount),
            static_cast<uint32_t>(sourceSampleRate),
            static_cast<uint16_t>(sourceChannelCount), convertedSampleSize);

    if (!hasValidConvertedSize) {
      mb = MessageBox::Create(*this, "Import Failed", "Sample is too large",
                              MBBF_OK);
    } else if (!pool->CheckSampleFits(convertedSampleSize)) {
      const uint32_t availableSpace = pool->GetAvailableSampleStorageSpace();
      char message[SCREEN_WIDTH];
      if (availableSpace >= convertedSampleSize) {
        npf_snprintf(message, sizeof(message), "Cannot fit in sample memory");
      } else {
        npf_snprintf(message, sizeof(message), "Only %u bytes free",
                     availableSpace);
      }
      // pad with trailing spaces as dialog width based on title length
      mb = MessageBox::Create(*this, "Sample Too Large       ", message,
                              MBBF_OK);
    }
    wav.Close();
  }

  if (mb != nullptr) {
    DoModal(mb);
    return;
  }

  int sampleID = pool->ImportSample(samplePath.c_str(), projName);

  if (sampleID >= 0) {
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> importedFilename;
    const char *storedName = pool->GetNameList()[sampleID];
    if (storedName != nullptr) {
      importedFilename = storedName;
    }

    I_Instrument *instr =
        viewData_->project_->GetInstrumentBank()->GetInstrument(toInstr_);
    if (instr->GetType() == IT_SAMPLE) {
      SampleInstrument *sinstr = static_cast<SampleInstrument *>(instr);
      sinstr->AssignSample(sampleID);
      sinstr->ClearSlices();
      if (isSingleCycle) {
        sinstr->FindVariable(FourCC::SampleInstrumentLoopMode)
            ->SetInt(SILM_OSC);
      }
    } else if (instr->GetType() == IT_SAMPLE_SOURCE) {
      SampleSourceInstrument *ssinstr =
          static_cast<SampleSourceInstrument *>(instr);
      ssinstr->AssignSample(sampleID);
      ssinstr->ClearSlices();
      if (isSingleCycle) {
        ssinstr->FindVariable(FourCC::SampleSourceInstrumentLoopMode)
            ->SetInt(SSLM_OSC);
        ssinstr->ResetOscillatorLoopModeIfSampleTooLarge();
      }
    }

    if (importedFilename.empty()) {
      importedFilename = sampleFilename;
    }

    // If the imported filename differs from the original basename, the sample
    // pool truncated it to a valid project filename.
    if (importedFilename != entry->name.c_str()) {
      Trace::Log("PICOIMPORT", "Sample filename truncated on import: %s -> %s",
                 entry->name.c_str(), importedFilename.c_str());
      MessageBox *mb = MessageBox::Create(*this, "Sample name Truncated!",
                                          importedFilename.c_str(), MBBF_OK);
      DoModal(mb);
    }
  } else if (sampleID == SamplePool::ImportDuplicateNameError) {
    // The pool refused the import because it would have overwritten the file
    // backing the sample already in the pool under that name.
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> projectSampleFilename;
    if (!SamplePool::GetProjectSampleFilename(samplePath.c_str(),
                                              projectSampleFilename)) {
      projectSampleFilename = sampleFilename;
    }
    MessageBox *mb = MessageBox::Create(*this, "Sample name already in pool",
                                        projectSampleFilename.c_str(), MBBF_OK);
    DoModal(mb);
  } else {
    Trace::Error("failed to import sample");
    // Show a generic error message if import failed for other reasons
    MessageBox *mb = MessageBox::Create(*this, "Import Failed",
                                        "Could not import sample", MBBF_OK);
    DoModal(mb);
  };
  isDirty_ = true;
};

void ImportView::adjustPreviewVolume(int offset) {
  // Get the project instance
  Project *project = viewData_->project_;

  // Find the preview volume variable
  Variable *v = project->FindVariable(FourCC::VarPreviewVolume);
  if (!v) {
    Status::Set("Preview volume setting not found");
    return;
  }

  // Get current value and apply offset
  int newVolume = v->GetInt() + offset;

  // Clamp volume to valid 0-99 as per other channels
  newVolume = newVolume > 99 ? 99 : newVolume;
  newVolume = newVolume < 0 ? 0 : newVolume;

  // Set the new value
  v->SetInt(newVolume);

  // Mark the view as dirty to update the status bar with the new volume
  isDirty_ = true;
}

void ImportView::setCurrentFolder(FileSystem *fs, const char *name,
                                  FolderNav nav) {
  if (fs == nullptr || name == nullptr) {
    Trace::Error("FAILED to set current folder: invalid args");
    return;
  }

  FileSystemPath::PathString nextDir;
  if (nav == FolderNav::Up) {
    const char *rootDir = inProjectSampleDir_ ? currentDir_.c_str() : "/";
    if (!BrowserPathUtils::ResolveParentDirectory(currentDir_.c_str(), rootDir,
                                                  nextDir)) {
      return;
    }
  } else if (FileSystemPath::IsAbsolutePath(name)) {
    if (!FileSystemPath::NormalizePath(name, nextDir)) {
      Trace::Error("FAILED to normalize folder %s", name);
      return;
    }
  } else if (!FileSystemPath::JoinPath(currentDir_.c_str(), name, nextDir)) {
    Trace::Error("FAILED to resolve folder %s from %s", name,
                 currentDir_.c_str());
    return;
  }

  if (nextDir == currentDir_) {
    // No actual move (e.g. re-focusing the browser in the same directory):
    // keep the position stack and selection, just refresh the listing in
    // case the tree changed while we were away.
    refreshFileIndexList(fs);
    return;
  }

  if (!inProjectSampleDir_) {
    char projName[MAX_PROJECT_NAME_LENGTH + 1] = {};
    viewData_->project_->GetProjectName(projName);
    if (BrowserPathUtils::IsCurrentProjectImportDirectory(nextDir.c_str(),
                                                          projName)) {
      Trace::Log("PICOIMPORT",
                 "NOT allowed to browse into current project directory");
      return;
    }
  }

  if (inProjectSampleDir_) {
    // The project pool listing has no subdirectory navigation, so there is
    // nothing to remember or restore.
    currentIndex_ = 0;
  } else if (nav == FolderNav::ResumeImport) {
    currentIndex_ = savedImportIndex_;
    topIndex_ = savedImportTopIndex_;
  } else if (nav == FolderNav::Up) {
    // Going up: restore the selection position we had in the parent dir.
    if (dirIndexDepth_ > 0) {
      --dirIndexDepth_;
      currentIndex_ = dirIndexStack_[dirIndexDepth_];
    } else {
      currentIndex_ = 0;
    }
  } else if (nav == FolderNav::DrillDown) {
    // Going down: remember where we are in this dir before leaving it.
    // Navigation is never blocked by history capacity: when the stack is
    // full the oldest (shallowest) record is dropped, so only the most
    // recent maxDirIndexStackDepth levels keep their position.
    if (dirIndexDepth_ >= maxDirIndexStackDepth) {
      std::memmove(&dirIndexStack_[0], &dirIndexStack_[1],
                   (maxDirIndexStackDepth - 1) * sizeof(uint16_t));
      --dirIndexDepth_;
    }
    dirIndexStack_[dirIndexDepth_] = static_cast<uint16_t>(currentIndex_);
    ++dirIndexDepth_;
    currentIndex_ = 0;
  } else {
    // Jump: the move does not follow the directory tree we have been
    // tracking, so the saved position stack is discarded.
    dirIndexDepth_ = 0;
    currentIndex_ = 0;
  }

  currentDir_ = nextDir;
  if (!inProjectSampleDir_) {
    viewData_->importViewStartDir = currentDir_.c_str();
  }
  refreshFileIndexList(fs);
}

void ImportView::showSampleEditor(etl::string<PFILENAME_SIZE> filename,
                                  bool isProjectSample) {
  constexpr uint16_t pcmFormat = 0x0001;
  auto file = FileSystem::GetInstance()->Open(filename.c_str(), "r");
  auto headerResult = WavHeaderWriter::ReadHeader(file.get());
  if (!headerResult.has_value()) {
    MessageBox *mb = MessageBox::Create(*this, "Can't Edit Sample",
                                        "Invalid WAV file", MBBF_OK);
    DoModal(mb);
    return;
  }

  const WavHeaderInfo &header = headerResult.value();
  if (header.audioFormat != pcmFormat || header.numChannels == 0 ||
      header.numChannels > 2 ||
      (header.bitsPerSample != 8 && header.bitsPerSample != 16) ||
      header.dataChunkSize == 0) {
    MessageBox *mb = MessageBox::Create(*this, "Can't Edit Sample",
                                        "Unsupported WAV format", MBBF_OK);
    DoModal(mb);
    return;
  }

  viewData_->sampleEditorFilename = filename;
  viewData_->isShowingSampleEditorProjectPool = isProjectSample;

  // before going to sample editor set this view as its "source" view
  SampleEditorView::sourceViewType_ = VT_IMPORT;

  // Switch to the SampleEditorView
  ViewType vt = VT_SAMPLE_EDITOR;
  ViewEvent ve(VET_SWITCH_VIEW, &vt);
  SetChanged();
  NotifyObservers(&ve);
}

void ImportView::renameSample(const FileSystemEntry &entry, FileSystem *fs) {
  if (fs == nullptr || entry.type != PFT_FILE) {
    return;
  }

  FileSystemPath::PathString samplePath;
  if (!resolveEntryPath(currentDir_, entry, currentDir_.c_str(), samplePath)) {
    return;
  }

  // Show only the basename without the .wav extension so the user edits
  // just the filename. The extension is appended back on confirm.
  etl::string<MAX_TEXT_INPUT_LENGTH> defaultName =
      stripWavExtension<MAX_TEXT_INPUT_LENGTH>(entry.name.c_str());
  TextInputModalView *renameModal = TextInputModalView::Create(
      *this, "Rename sample", "Name:", MBBF_OK | MBBF_CANCEL, defaultName);
  strncpy(pendingRenamePath_, samplePath.c_str(), PFILENAME_SIZE - 1);
  pendingRenamePath_[PFILENAME_SIZE - 1] = '\0';
  DoModal(
      renameModal,
      ModalViewCallback::create<ImportView, &ImportView::onConfirmRenameSample>(
          *this));
}

void ImportView::onConfirmRenameSample(View &, ModalView &dialog) {
  FileSystem *fs = FileSystem::GetInstance();
  char oldPath[PFILENAME_SIZE];
  strncpy(oldPath, pendingRenamePath_, PFILENAME_SIZE - 1);
  oldPath[PFILENAME_SIZE - 1] = '\0';
  pendingRenamePath_[0] = '\0';

  if (dialog.GetReturnCode() != MBL_OK || fs == nullptr || oldPath[0] == '\0') {
    return;
  }

  auto *renameModal = static_cast<TextInputModalView *>(&dialog);
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> newName =
      renameModal->GetTextField()->GetString().c_str();
  if (newName.empty() || hasPathSeparator(newName.c_str()) ||
      !ensureWavExtension(newName)) {
    MessageBox *mb = MessageBox::Create(*this, "Invalid filename", MBBF_OK);
    DoModal(mb);
    return;
  }

  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> oldName;
  if (!extractLeafName(oldPath, oldName)) {
    Trace::Error("PICOIMPORT: Failed to extract leaf name from %s", oldPath);
    return;
  }
  if (oldName == newName) {
    return;
  }

  FileSystemPath::PathString newPath;
  if (!FileSystemPath::JoinPath(currentDir_.c_str(), newName.c_str(),
                                newPath)) {
    MessageBox *mb = MessageBox::Create(*this, "Invalid filename", MBBF_OK);
    DoModal(mb);
    return;
  }
  if (fs->exists(newPath.c_str())) {
    MessageBox *mb = MessageBox::Create(*this, "File exists", MBBF_OK);
    DoModal(mb);
    return;
  }

  Player::GetInstance()->StopStreaming();
  if (!fs->MoveFile(oldPath, newPath.c_str())) {
    Trace::Error("PICOIMPORT: Failed to rename sample %s -> %s", oldPath,
                 newPath.c_str());
    MessageBox *mb = MessageBox::Create(*this, "Rename failed", MBBF_OK);
    DoModal(mb);
    return;
  }

  if (inProjectSampleDir_) {
    auto *pool = SamplePool::GetInstance();
    if (pool->FindSampleIndexByName(oldName) != static_cast<uint32_t>(-1)) {
      pool->RenameLoadedSample(oldName, newName);
    }
  }

  refreshFileIndexList(fs);
  isDirty_ = true;
}

void ImportView::removeProjectSample(const FileSystemEntry &entry,
                                     FileSystem *fs) {
  FileSystemPath::PathString samplePath;
  if (!resolveEntryPath(currentDir_, entry, currentDir_.c_str(), samplePath)) {
    return;
  }

  // first check if a instrument uses this sample
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> leafName;
  if (!extractLeafName(samplePath.c_str(), leafName)) {
    Trace::Error("PICOIMPORT: Failed to extract leaf name from %s",
                 samplePath.c_str());
    return;
  }

  auto sampleIndexInUse = viewData_->project_->SampleInUse(leafName);

  if (sampleIndexInUse > -1) {
    etl::string<40> inUseByInstrument("Sample in use by I");
    etl::to_string(sampleIndexInUse, inUseByInstrument, true);
    MessageBox *mb = MessageBox::Create(*this, "Cannot delete",
                                        inUseByInstrument.c_str(), MBBF_OK);
    DoModal(mb);
    return;
  }

  // add spacing for basic way to size dialog wider to give Ok/cancel
  // buttons between space
  MessageBox *mb =
      MessageBox::Create(*this, "    Remove sample?    ", entry.name.c_str(),
                         MBBF_OK | MBBF_CANCEL);
  strncpy(pendingDeletePath_, samplePath.c_str(), PFILENAME_SIZE - 1);
  pendingDeletePath_[PFILENAME_SIZE - 1] = '\0';
  DoModal(mb,
          ModalViewCallback::create<ImportView,
                                    &ImportView::onConfirmRemoveProjectSample>(
              *this));
}

void ImportView::onConfirmRemoveProjectSample(View &, ModalView &dialog) {
  FileSystem *fs = FileSystem::GetInstance();

  char path[PFILENAME_SIZE];
  strncpy(path, pendingDeletePath_, PFILENAME_SIZE - 1);
  path[PFILENAME_SIZE - 1] = '\0';
  pendingDeletePath_[0] = '\0';

  if (dialog.GetReturnCode() != MBL_OK || fs == nullptr) {
    return;
  }

  // Ensure preview playback cannot keep the project-pool file locked.
  Player::GetInstance()->StopStreaming();

  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> leafName;
  if (!extractLeafName(path, leafName)) {
    Trace::Error("Failed to extract leaf name for %s", path);
    return;
  }

  auto *pool = SamplePool::GetInstance();
  const int32_t sampleIndex =
      static_cast<int32_t>(pool->FindSampleIndexByName(leafName));
  if (sampleIndex < 0) {
    Trace::Error("ImportView: Sample %s could not be deleted because it is "
                 "still in use or not safely removable",
                 leafName.c_str());
    MessageBox *mb =
        MessageBox::Create(*this, "Cannot delete", "sample playing", MBBF_OK);
    DoModal(mb);
    return;
  }

  if (!fs->DeleteFile(path)) {
    Trace::Error("Failed to delete sample %s", path);
    return;
  }

  pool->unloadSample(static_cast<uint32_t>(sampleIndex));

  if (currentIndex_ > 0) {
    --currentIndex_;
  }

  refreshFileIndexList(fs);
  isDirty_ = true;
}

void ImportView::refreshFileIndexList(FileSystem *fs) {
  invalidateSelectedSamplePoolSize();
  if (fs == nullptr || currentDir_.empty()) {
    fileEntries_.clear();
    topIndex_ = 0;
    currentIndex_ = 0;
    return;
  }

  fileEntries_.clear();
  const bool listed =
      fs->ListDirectory(currentDir_.c_str(), &fileEntries_, ".wav", false);
  const bool truncated = isDirectoryListTruncated(fs);
  if (!listed && !truncated) {
    Trace::Error("PICOIMPORT: Failed to list %s", currentDir_.c_str());
    topIndex_ = 0;
    currentIndex_ = 0;
    return;
  }
  if (truncated) {
    Trace::Error("PICOIMPORT: Truncated listing for %s", currentDir_.c_str());
    showDirectoryListTruncatedDialog(*this);
  }

  SortBrowserEntries(fileEntries_);

  if (!inProjectSampleDir_) {
    BrowserPathUtils::PrependParentEntry(currentDir_.c_str(), "/",
                                         &fileEntries_);
  }

  if (currentIndex_ >= fileEntries_.size() && !fileEntries_.empty()) {
    currentIndex_ = fileEntries_.size() - 1;
  }
  if (fileEntries_.empty()) {
    topIndex_ = 0;
    currentIndex_ = 0;
  }
}
