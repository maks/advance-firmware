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

#include "InstrumentImportView.h"
#include "Application/Instruments/AdvanceFM6/FM6Dx7Patch.h"
#include "Application/Instruments/DecentSamplerPreset.h"
#include "Application/Instruments/FM6Instrument.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/Player.h"
#include "Application/Views/BaseClasses/BrowserEntrySort.h"
#include "Application/Views/BaseClasses/BrowserPaging.h"
#include "Application/Views/BaseClasses/BrowserPathUtils.h"
#include "DslibraryExtractor.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/to_string.h"
#include "ModalDialogs/MessageBox.h"
#include "System/FileSystem/I_File.h"
#include "System/FileSystem/PathUtils.h"
#include <cstring>
#include <memory>
#include <nanoprintf.h>
#include <strings.h>
#include <tinyxml2.h>

#define LIST_WIDTH (SCREEN_WIDTH - 2)
// -4 to allow for title, filesize & spacers
#define LIST_PAGE_SIZE (SCREEN_HEIGHT - 4)

namespace {
constexpr size_t kImportBrowserPageCapacity = MAX_FILE_INDEX_SIZE - 1;
#if defined(HOST_TEST)
#define DSLIBRARY_PAGE_STORAGE
#else
#define DSLIBRARY_PAGE_STORAGE                                                 \
  __attribute__((section(".RAM_EXT"))) __attribute__((aligned(32)))
#endif
etl::vector<FileSystemEntry, kImportBrowserPageCapacity> dslibraryPageEntries
    DSLIBRARY_PAGE_STORAGE;
#undef DSLIBRARY_PAGE_STORAGE

template <size_t Capacity>
etl::string<Capacity> makeBoundedInstrumentName(const char *name) {
  etl::string<Capacity> boundedName;
  if (name == nullptr) {
    return boundedName;
  }

  const size_t nameLength = std::strlen(name);
  if (nameLength <= boundedName.capacity()) {
    boundedName = name;
    return boundedName;
  }

  constexpr const char *extension = ".dspreset";
  constexpr size_t extensionLength = sizeof(".dspreset") - 1;
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

  leafName = makeBoundedInstrumentName<MAX_INSTRUMENT_FILENAME_LENGTH>(
      baseName.c_str());
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
                      const FileSystemEntry &entry, const char *rootDirectory,
                      FileSystemPath::PathString &resolvedPath) {
  if (entry.name == BrowserPathUtils::kParentDirectoryName) {
    return BrowserPathUtils::ResolveParentDirectory(
        currentDir.c_str(), rootDirectory, resolvedPath);
  }

  return FileSystemPath::JoinPath(currentDir.c_str(), entry.name.c_str(),
                                  resolvedPath);
}

// Whole-file staging buffer so a candidate .syx is read from the SD card in
// one go; its 32 voice records back the virtual patch listing until the view
// leaves it.
#if defined(HOST_TEST)
#define DX7_STAGING_ATTRIBUTES __attribute__((aligned(32)))
#else
#define DX7_STAGING_ATTRIBUTES                                                 \
  __attribute__((section(".SDRAM1_NC"))) __attribute__((aligned(32)))
#endif

static etl::array<uint8_t, fm6::Dx7BankSysexSize> stagedSysex
    DX7_STAGING_ATTRIBUTES;

#undef DX7_STAGING_ATTRIBUTES

constexpr const char *kSysexBackName = "Back";

#if defined(HOST_TEST)
#define DS_IMPORT_ATTRIBUTES __attribute__((aligned(32)))
#else
#define DS_IMPORT_ATTRIBUTES                                                   \
  __attribute__((section(".SDRAM1_NC"))) __attribute__((aligned(32)))
#endif
static DecentSamplerPreset importPreset DS_IMPORT_ATTRIBUTES;
static FileSystemPath::PathString importedDecentSamplerPath;
#undef DS_IMPORT_ATTRIBUTES

void removeImportedPresetFiles(const char *directory, uint16_t zoneCount,
                               bool removePreset) {
  auto *fs = FileSystem::GetInstance();
  FileSystemPath::PathString path;
  if (removePreset) {
    FileSystemPath::PathString folderLeaf;
    char presetFileName[MAX_INSTRUMENT_FILENAME_LENGTH + 16]{};
    if (FileSystemPath::BaseName(directory, folderLeaf)) {
      npf_snprintf(presetFileName, sizeof(presetFileName), "%s.dspreset",
                   folderLeaf.c_str());
      if (FileSystemPath::JoinPath(directory, presetFileName, path)) {
        fs->DeleteFile(path.c_str());
      }
    }
  }
  for (uint16_t i = 0; i < zoneCount; ++i) {
    char fileName[20];
    npf_snprintf(fileName, sizeof(fileName), "sample%03u.wav", i);
    if (FileSystemPath::JoinPath(directory, fileName, path)) {
      fs->DeleteFile(path.c_str());
    }
  }
  fs->DeleteDir(directory);
}

void removeImportedPreset(const char *presetPath) {
  FileSystemPath::PathString directory;
  if (FileSystemPath::DirName(presetPath, directory)) {
    removeImportedPresetFiles(directory.c_str(), DecentSamplerPreset::MaxZones,
                              true);
  }
}

bool buildImportedDecentSampler(const char *sourcePath, const char *projectName,
                                FileSystemPath::PathString &importedPath,
                                const char *&error,
                                SampleConversionErrorCode &conversionError) {
  conversionError = SampleConversionErrorCode::None;
  error = "Could not read DecentSampler preset";
  auto result = LoadDecentSamplerPreset(sourcePath, importPreset);
  if (!result) {
    error = result.message_;
    return false;
  }

  auto *fs = FileSystem::GetInstance();
  uint64_t requiredBytes = 1024ULL * 1024ULL;
  for (uint16_t index = 0; index < importPreset.zoneCount_; ++index) {
    bool alreadyCounted = false;
    for (uint16_t previous = 0; previous < index; ++previous) {
      if (std::strcmp(importPreset.zones_[index].path_,
                      importPreset.zones_[previous].path_) == 0) {
        alreadyCounted = true;
        break;
      }
    }
    if (alreadyCounted) {
      continue;
    }
    const auto &zone = importPreset.zones_[index];
    const uint64_t rate = zone.sampleRate_ == 0 ? 44100U : zone.sampleRate_;
    const uint64_t outputFrames =
        (static_cast<uint64_t>(zone.frameCount_) * 44100U + rate - 1U) / rate;
    const uint64_t outputBytes =
        outputFrames * zone.channelCount_ * 2ULL + 44ULL;
    if (outputFrames > (UINT64_MAX - requiredBytes) /
                           (static_cast<uint64_t>(zone.channelCount_) * 2ULL) ||
        outputBytes > UINT64_MAX - requiredBytes) {
      error = "Imported sample data is too large";
      return false;
    }
    requiredBytes += outputBytes;
  }
  uint64_t freeBytes = 0;
  if (!fs->GetFreeSpaceBytes("/projects", freeBytes)) {
    error = "Could not check SD free space";
    return false;
  }
  if (freeBytes < requiredBytes) {
    error = "Not enough SD space for imported samples";
    return false;
  }

  FileSystemPath::PathString projectPath, instrumentsPath, baseName;
  if (!FileSystemPath::JoinPath("/projects", projectName, projectPath) ||
      !FileSystemPath::JoinPath(projectPath.c_str(), "instruments",
                                instrumentsPath) ||
      !FileSystemPath::BaseName(sourcePath, baseName)) {
    error = "Project path is too long";
    return false;
  }
  if (!fs->makeDir(instrumentsPath.c_str(), true)) {
    error = "Could not create project instruments folder";
    return false;
  }

  char baseNameBuffer[MAX_INSTRUMENT_FILENAME_LENGTH + 1]{};
  const char *extension = std::strrchr(baseName.c_str(), '.');
  const size_t baseNameLength =
      extension == nullptr ? baseName.size()
                           : static_cast<size_t>(extension - baseName.c_str());
  if (baseNameLength == 0) {
    error = "Preset filename is invalid";
    return false;
  }
  const size_t boundedBaseNameLength =
      baseNameLength < MAX_INSTRUMENT_FILENAME_LENGTH
          ? baseNameLength
          : MAX_INSTRUMENT_FILENAME_LENGTH;
  std::memcpy(baseNameBuffer, baseName.c_str(), boundedBaseNameLength);

  FileSystemPath::PathString outputDirectory;
  bool foundDirectory = false;
  for (uint16_t suffix = 0; suffix < 1000; ++suffix) {
    char suffixText[8]{};
    if (suffix != 0) {
      npf_snprintf(suffixText, sizeof(suffixText), "-%u", suffix);
    }
    const size_t suffixLength = std::strlen(suffixText);
    const size_t folderBaseLength =
        boundedBaseNameLength + suffixLength <= MAX_INSTRUMENT_FILENAME_LENGTH
            ? boundedBaseNameLength
            : MAX_INSTRUMENT_FILENAME_LENGTH - suffixLength;
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> folderName;
    folderName.assign(baseNameBuffer, folderBaseLength);
    folderName.append(suffixText);
    if (folderName.is_truncated()) {
      error = "Preset name is too long";
      return false;
    }
    if (!FileSystemPath::JoinPath(instrumentsPath.c_str(), folderName.c_str(),
                                  outputDirectory)) {
      error = "Preset name is too long";
      return false;
    }
    if (!fs->exists(outputDirectory.c_str())) {
      foundDirectory = true;
      break;
    }
  }
  if (!foundDirectory || !fs->makeDir(outputDirectory.c_str(), false)) {
    error = "Could not create preset folder";
    return false;
  }

  SamplePool *pool = SamplePool::GetInstance();
  for (uint16_t i = 0; i < importPreset.zoneCount_; ++i) {
    bool alreadyConverted = false;
    for (uint16_t previous = 0; previous < i; ++previous) {
      if (std::strcmp(importPreset.zones_[i].path_,
                      importPreset.zones_[previous].path_) == 0) {
        alreadyConverted = true;
        break;
      }
    }
    if (alreadyConverted) {
      continue;
    }
    char fileName[20];
    npf_snprintf(fileName, sizeof(fileName), "sample%03u.wav", i);
    FileSystemPath::PathString outputSamplePath;
    if (!FileSystemPath::JoinPath(outputDirectory.c_str(), fileName,
                                  outputSamplePath)) {
      conversionError = SampleConversionErrorCode::InvalidOutputPath;
    } else if (!FileSystemPath::IsAbsolutePath(importPreset.zones_[i].path_) ||
               std::strlen(importPreset.zones_[i].path_) >= PFILENAME_SIZE) {
      conversionError = SampleConversionErrorCode::InvalidSourcePath;
    } else if (!pool->ConvertSampleFile(importPreset.zones_[i].path_,
                                        outputSamplePath.c_str(),
                                        conversionError)) {
      // The converter sets the specific failure code.
    }
    if (conversionError != SampleConversionErrorCode::None) {
      error = nullptr;
      removeImportedPresetFiles(outputDirectory.c_str(), i + 1, false);
      return false;
    }
  }

  FileSystemPath::PathString folderLeaf;
  char presetFileName[MAX_INSTRUMENT_FILENAME_LENGTH + 16]{};
  if (!FileSystemPath::BaseName(outputDirectory.c_str(), folderLeaf)) {
    error = "Preset path is too long";
    removeImportedPresetFiles(outputDirectory.c_str(), importPreset.zoneCount_,
                              false);
    return false;
  }
  npf_snprintf(presetFileName, sizeof(presetFileName), "%s.dspreset",
               folderLeaf.c_str());
  importedPath.clear();
  if (!FileSystemPath::JoinPath(outputDirectory.c_str(), presetFileName,
                                importedPath)) {
    error = "Preset path is too long";
    removeImportedPresetFiles(outputDirectory.c_str(), importPreset.zoneCount_,
                              false);
    return false;
  }
  auto output = fs->Open(importedPath.c_str(), "w");
  if (!output) {
    error = "Could not save imported preset";
    removeImportedPresetFiles(outputDirectory.c_str(), importPreset.zoneCount_,
                              false);
    return false;
  }

  tinyxml2::XMLPrinter printer(output.get());
  printer.OpenElement("DecentSampler");
  printer.OpenElement("groups");
  printer.OpenElement("group");
  for (uint16_t i = 0; i < importPreset.zoneCount_; ++i) {
    uint16_t fileIndex = i;
    for (uint16_t previous = 0; previous < i; ++previous) {
      if (std::strcmp(importPreset.zones_[i].path_,
                      importPreset.zones_[previous].path_) == 0) {
        fileIndex = previous;
        break;
      }
    }
    char fileName[20];
    npf_snprintf(fileName, sizeof(fileName), "sample%03u.wav", fileIndex);
    printer.OpenElement("sample");
    printer.PushAttribute("path", fileName);
    printer.PushAttribute("rootNote", importPreset.zones_[i].rootNote_);
    printer.PushAttribute("loNote", importPreset.zones_[i].lowNote_);
    printer.PushAttribute("hiNote", importPreset.zones_[i].highNote_);
    printer.PushAttribute("loVel", importPreset.zones_[i].lowVelocity_);
    printer.PushAttribute("hiVel", importPreset.zones_[i].highVelocity_);
    const uint32_t sampleRate = importPreset.zones_[i].sampleRate_;
    const uint64_t convertedStart =
        static_cast<uint64_t>(importPreset.zones_[i].startFrame_) * 44100U /
        sampleRate;
    if (convertedStart != 0) {
      printer.PushAttribute("start", convertedStart);
    }
    if (importPreset.zones_[i].endFrame_ != 0) {
      const uint64_t convertedEnd =
          (static_cast<uint64_t>(importPreset.zones_[i].endFrame_) * 44100U +
           sampleRate - 1U) /
          sampleRate;
      printer.PushAttribute("end", convertedEnd - 1);
    }
    printer.CloseElement();
  }
  printer.CloseElement();
  printer.CloseElement();
  printer.CloseElement();
  output.reset();
  if (fs->exists(importedPath.c_str()) &&
      LoadDecentSamplerPreset(importedPath.c_str(), importPreset)) {
    return true;
  }
  error = "Could not write imported preset";
  removeImportedPresetFiles(outputDirectory.c_str(),
                            DecentSamplerPreset::MaxZones, true);
  return false;
}

const uint8_t *stagedVoiceRecord(size_t patchIndex) {
  return stagedSysex.data() + fm6::Dx7BankHeaderSize +
         (patchIndex * fm6::Dx7PatchDataSize);
}

// Reads a whole bank .syx into stagedSysex and validates it. Nothing else is
// touched, so a bad file never disturbs the instrument.
fm6::Dx7BankParseResult readAndValidateSysexFile(const char *path) {
  auto file = FileSystem::GetInstance()->Open(path, "r");
  if (!file) {
    return fm6::Dx7BankParseResult::ReadError;
  }
  size_t total = 0;
  while (total < stagedSysex.size()) {
    const int read = file->Read(stagedSysex.data() + total,
                                static_cast<int>(stagedSysex.size() - total));
    if (read < 0) {
      return fm6::Dx7BankParseResult::ReadError;
    }
    if (read == 0) {
      break;
    }
    total += static_cast<size_t>(read);
  }
  uint8_t extra = 0;
  const int extraRead = file->Read(&extra, 1);
  if (extraRead < 0 || file->Error() != 0) {
    return fm6::Dx7BankParseResult::ReadError;
  }
  if (total != stagedSysex.size() || extraRead > 0) {
    return fm6::Dx7BankParseResult::BadSize;
  }
  return fm6::ValidateDx7BankSysex(stagedSysex);
}

const char *parseResultMessage(fm6::Dx7BankParseResult result) {
  switch (result) {
  case fm6::Dx7BankParseResult::BadChecksum:
    return "Invalid DX7 checksum";
  case fm6::Dx7BankParseResult::ReadError:
    return "Could not read file";
  default:
    return "Invalid DX7 bank file";
  }
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

InstrumentImportView::Mode InstrumentImportView::mode_ =
    InstrumentImportView::Mode::Instrument;

InstrumentImportView::InstrumentImportView(GUIWindow &w, ViewData *viewData)
    : ScreenView(w, viewData), fileEntries_(GetInstrumentBrowserFileEntries()) {
  toInstrID_ = viewData_->currentInstrumentID_;
}

InstrumentImportView::~InstrumentImportView() {}

void InstrumentImportView::SetMode(Mode mode) { mode_ = mode; }

void InstrumentImportView::Reset() {
  topIndex_ = 0;
  currentIndex_ = 0;
  listingOffset_ = 0;
  listingTotal_ = 0;
  selected_ = 0;
  toInstrID_ = 0;
  browsingSysex_ = false;
  dslibraryPresetChooser_ = false;
  lastRecoveryScanPath_.clear();
  previewKeyHeld_ = false;
  currentDir_.clear();
  fileEntries_.clear();
}

void InstrumentImportView::ProcessButtonMask(unsigned short mask,
                                             bool pressed) {
  if (extractingDslibrary_) {
    if (pressed && (mask & EPBM_LEFT) && (mask & EPBM_NAV)) {
      DslibraryExtractor::Cancel();
    }
    return;
  }
  if (!pressed) {
    if (previewKeyHeld_ && !(mask & EPBM_PLAY)) {
      stopPatchPreview();
    }
    return;
  }

  if (mask & EPBM_PLAY) {
    const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
    if (browsingSysex_) {
      if (entry != nullptr && entry->type == PFT_FILE && currentIndex_ > 0) {
        startPatchPreview(stagedVoiceRecord(currentIndex_ - 1));
      }
      return;
    }
    FileSystemPath::PathString instrumentPath;
    if (entry != nullptr && (mask & EPBM_ALT) && entry->type == PFT_FILE &&
        mode_ == Mode::Instrument) {
      if (!resolveEntryPath(currentDir_, *entry, INSTRUMENTS_DIR,
                            instrumentPath)) {
        return;
      }
      Trace::Log("INSTRUMENTIMPORT", "SHIFT play - import");
      importInstrument(instrumentPath.c_str());
    } else {
      // TODO: audition instrument by temporarily loading it and playing a note
    }

    // handle moving up and down the file list
  } else if (dslibraryPresetChooser_ && (mask & EPBM_EDIT) &&
             (mask & (EPBM_UP | EPBM_DOWN))) {
    if (mask & EPBM_DOWN) {
      if (listingOffset_ + kImportBrowserPageCapacity < listingTotal_) {
        listingOffset_ += kImportBrowserPageCapacity;
      }
    } else if (listingOffset_ >= kImportBrowserPageCapacity) {
      listingOffset_ -= kImportBrowserPageCapacity;
    }
    setCurrentFolder(FileSystem::GetInstance(), currentDir_.c_str());
  } else if (mask & EPBM_UP) {
    const bool page = (mask & EPBM_EDIT) != 0;
    warpToNextInstrument(true, page);
  } else if (mask & EPBM_DOWN) {
    const bool page = (mask & EPBM_EDIT) != 0;
    warpToNextInstrument(false, page);
  } else if ((mask & EPBM_LEFT) && (mask & EPBM_NAV)) {
    stopPatchPreview();
    ViewType vt = VT_INSTRUMENT;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  } else {
    if (mask & EPBM_ENTER) {
      auto fs = FileSystem::GetInstance();
      const auto *entry = getCurrentEntry(fileEntries_, currentIndex_);
      if (entry == nullptr) {
        return;
      }

      if (browsingSysex_) {
        if (entry->name == kSysexBackName) {
          leaveSysexListing();
        } else if (currentIndex_ > 0) {
          commitSysexPatch(currentIndex_ - 1);
        }
        return;
      }

      const bool dx7File = mode_ != Mode::Instrument;
      FileSystemPath::PathString selectedPath;
      const char *rootDirectory =
          dx7File ? "/"
                  : (dslibraryPresetChooser_ ? dslibraryRoot_.c_str()
                                             : INSTRUMENTS_DIR);
      if (dslibraryPresetChooser_ && entry->name == ".." &&
          currentDir_ == dslibraryRoot_) {
        dslibraryPresetChooser_ = false;
        setCurrentFolder(fs, INSTRUMENTS_DIR);
        return;
      }
      if (!resolveEntryPath(currentDir_, *entry, rootDirectory, selectedPath)) {
        // In Dx7Patch mode (entered from the FM6 preset browser) ".." at the
        // filesystem root has no parent to resolve; it returns to the preset
        // browser instead.
        if (mode_ == Mode::Dx7Patch && entry->name == "..") {
          stopPatchPreview();
          ViewType vt = VT_FM6_PRESETS;
          ViewEvent ve(VET_SWITCH_VIEW, &vt);
          SetChanged();
          NotifyObservers(&ve);
        }
        return;
      }

      if (entry->type == PFT_DIR) {
        setCurrentFolder(fs, selectedPath.c_str());
        isDirty_ = true;
        topIndex_ = 0;
      } else if (dx7File) {
        enterSysexFile(selectedPath.c_str());
      } else {
        if (!startDslibraryImport(selectedPath.c_str())) {
          importInstrument(selectedPath.c_str());
        }
      }
    }
  }
};

void InstrumentImportView::DrawView() {
  Clear();

  // Draw title
  setTitle(etl::string<23>(mode_ != Mode::Instrument ? "Import DX7 Patch"
                                                     : "Import Instrument"));

  GUITextProperties props;

  SetColor(CD_NORMAL);

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
        if (mode_ == Mode::Dx7Patch && currentDir_ == "/") {
          DrawRegionString(TEXT_REGION_BODY, x, y, "[System]", props);
        } else {
          DrawRegionString(TEXT_REGION_BODY, x, y,
                           BrowserPathUtils::kParentDirectoryDisplayName,
                           props);
        }
      } else {
        DrawRegionString(TEXT_REGION_BODY, x, y, "[", props);
        DrawRegionString(TEXT_REGION_BODY, x + 1, y, entry.name.c_str(), props);
        DrawRegionString(TEXT_REGION_BODY, x + entry.name.size() + 1, y, "]",
                         props);
      }
    } else if (browsingSysex_) {
      // Hex index drawn in the row-label accent colour, then the patch name,
      // then the source bank name drawn at a fixed column so the
      // bank names line up regardless of patch-name length. Index 0 is the
      // "SDCard" dir entry, so preset program == list index - 1.
      SetColor(CD_ACCENT);
      DrawRegionString(TEXT_REGION_BODY, x, y, entry.name.c_str(), props);
      const size_t prefixLen = 3; // "0x "
      SetColor(i == currentIndex_ ? CD_HILITE2 : CD_NORMAL);
      DrawRegionString(TEXT_REGION_BODY, x + prefixLen, y,
                       entry.name.c_str() + prefixLen, props);
    } else {
      DrawRegionString(TEXT_REGION_BODY, x, y, entry.name.c_str(), props);
    }
    y++;
  }
  if (extractingDslibrary_) {
    DslibraryExtractor::Progress progress{};
    char stage[24]{};
    char filename[PFILENAME_SIZE]{};
    DslibraryExtractor::GetAsyncProgress(progress, stage, sizeof(stage),
                                         filename, sizeof(filename));
    SetColor(CD_ACCENT);
    DrawRegionString(TEXT_REGION_BODY, 1, LIST_PAGE_SIZE + 1,
                     progress.stage == nullptr ? "Inspecting archive"
                                               : progress.stage,
                     props);
    DrawRegionString(TEXT_REGION_BODY, 1, LIST_PAGE_SIZE + 2, filename, props);
    char progressText[40]{};
    npf_snprintf(progressText, sizeof(progressText), "%llu / %llu bytes",
                 static_cast<unsigned long long>(progress.extractedBytes),
                 static_cast<unsigned long long>(progress.totalBytes));
    DrawRegionString(TEXT_REGION_BODY, 1, LIST_PAGE_SIZE + 3, progressText,
                     props);
  } else if (dslibraryPresetChooser_ &&
             listingTotal_ > kImportBrowserPageCapacity) {
    char pageText[40]{};
    const size_t page = listingOffset_ / kImportBrowserPageCapacity + 1;
    const size_t pages = (listingTotal_ + kImportBrowserPageCapacity - 1) /
                         kImportBrowserPageCapacity;
    npf_snprintf(pageText, sizeof(pageText), "Page %u/%u EDIT+UP/DOWN",
                 static_cast<uint32_t>(page), static_cast<uint32_t>(pages));
    SetColor(CD_ACCENT);
    DrawRegionString(TEXT_REGION_BODY, 1, LIST_PAGE_SIZE + 1, pageText, props);
  }
};

void InstrumentImportView::OnPlayerUpdate(PlayerEventType, unsigned int) {
  updateDslibraryImport();
}

void InstrumentImportView::AnimationUpdate() {
  ScreenView::AnimationUpdate();
  updateDslibraryImport();
}

void InstrumentImportView::updateDslibraryImport() {
  if (!extractingDslibrary_) {
    return;
  }
  DslibraryExtractor::Result result;
  if (DslibraryExtractor::TakeAsyncResult(result)) {
    extractingDslibrary_ = false;
    if (result == DslibraryExtractor::Result::Success) {
      char presetPath[PFILENAME_SIZE]{};
      const uint32_t presets =
          DslibraryExtractor::GetPresetSummary(presetPath, sizeof(presetPath));
      if (presets == 0) {
        MessageBox *mb = MessageBox::Create(*this, "No presets found", MBBF_OK);
        DoModal(mb);
      } else if (presets == 1) {
        FileSystemPath::PathString fullPresetPath;
        if (FileSystemPath::JoinPath(dslibraryRoot_.c_str(), presetPath,
                                     fullPresetPath)) {
          importInstrument(fullPresetPath.c_str());
        } else {
          MessageBox *mb =
              MessageBox::Create(*this, "Preset path too long", MBBF_OK);
          DoModal(mb);
        }
      } else {
        dslibraryPresetChooser_ = true;
        setCurrentFolder(FileSystem::GetInstance(), dslibraryRoot_.c_str());
        MessageBox *mb = MessageBox::Create(
            *this, "Select a preset from the library", MBBF_OK);
        DoModal(mb);
      }
    } else if (result != DslibraryExtractor::Result::Cancelled) {
      const char *message = "Library extraction failed";
      if (result == DslibraryExtractor::Result::ExistingDestination) {
        message = "Destination already exists";
      } else if (result == DslibraryExtractor::Result::InsufficientSpace) {
        message = "Not enough SD space";
      } else if (result == DslibraryExtractor::Result::UnsupportedArchive) {
        message = "Unsupported ZIP archive";
      } else if (result == DslibraryExtractor::Result::InvalidPath) {
        message = "Invalid archive path";
      } else if (result == DslibraryExtractor::Result::OutOfMemory) {
        message = "Library workspace exhausted";
      }
      MessageBox *mb = MessageBox::Create(*this, message, MBBF_OK);
      DoModal(mb);
    }
    isDirty_ = true;
  } else {
    isDirty_ = true;
  }
}

void InstrumentImportView::OnFocus() {
  toInstrID_ = viewData_->currentInstrumentID_;
  browsingSysex_ = false;
  dslibraryPresetChooser_ = false;
  lastRecoveryScanPath_.clear();
  previewKeyHeld_ = false;
  auto fs = FileSystem::GetInstance();
  setCurrentFolder(fs, mode_ != Mode::Instrument ? "/" : INSTRUMENTS_DIR);
}

void InstrumentImportView::warpToNextInstrument(bool goUp, bool page) {
  BrowserPaging::Warp(currentIndex_, topIndex_, fileEntries_.size(),
                      LIST_PAGE_SIZE, goUp, page);
  isDirty_ = true;
}

void InstrumentImportView::importInstrument(const char *name) {
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> leafName;
  if (!extractLeafName(name, leafName)) {
    Trace::Error("INSTRUMENTIMPORT: Failed to extract leaf name from %s", name);
    MessageBox *mb =
        MessageBox::Create(*this, "Invalid instrument file", MBBF_OK);
    DoModal(mb);
    return;
  }

  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  assert(bank);

  toInstrID_ = viewData_->currentInstrumentID_;

  InstrumentType importedType =
      PersistencyService::GetInstance()->DetectInstrumentType(name);

  if (importedType == IT_NONE) {
    MessageBox *mb =
        MessageBox::Create(*this, "Unknown instrument type", MBBF_OK);
    DoModal(mb);
    return;
  }

  const char *instrumentPath = name;
  if (importedType == IT_MSAMPLER) {
    char projectName[MAX_PROJECT_NAME_LENGTH + 1]{};
    viewData_->project_->GetProjectName(projectName);
    const char *error = nullptr;
    SampleConversionErrorCode conversionError = SampleConversionErrorCode::None;
    if (!buildImportedDecentSampler(name, projectName,
                                    importedDecentSamplerPath, error,
                                    conversionError)) {
      char errorMessage[32];
      if (conversionError != SampleConversionErrorCode::None) {
        npf_snprintf(errorMessage, sizeof(errorMessage),
                     "Sample import failed [%u]",
                     static_cast<uint32_t>(conversionError));
        error = errorMessage;
      }
      MessageBox *mb = MessageBox::Create(
          *this, "DecentSampler import failed",
          error == nullptr ? "Could not import preset" : error, MBBF_OK);
      DoModal(mb);
      return;
    }
    instrumentPath = importedDecentSamplerPath.c_str();
  }

  Trace::Log("INSTRUMENTIMPORT", "Detected instrument type: %d", importedType);
  Trace::Log("INSTRUMENTIMPORT", "Importing instrument from file: %s", name);
  Trace::Log("INSTRUMENTIMPORT", "Target instrument ID: %d", toInstrID_);

  I_Instrument *currentInstrument = bank->GetInstrument(toInstrID_);

  if (!currentInstrument) {
    MessageBox *mb = MessageBox::Create(*this, "Invalid instrument", MBBF_OK);
    DoModal(mb);
    return;
  }

  Trace::Log("INSTRUMENTIMPORT",
             "Current instrument type: %d, Imported type: %d",
             currentInstrument->GetType(), importedType);

  if (currentInstrument->GetType() != importedType) {
    Trace::Log("INSTRUMENTIMPORT", "Converting instrument from type %d to %d",
               currentInstrument->GetType(), importedType);

    bank->releaseInstrument(toInstrID_);

    if (bank->GetNextAndAssignID(importedType, toInstrID_) ==
        NO_MORE_INSTRUMENT) {
      if (importedType == IT_MSAMPLER) {
        removeImportedPreset(importedDecentSamplerPath.c_str());
      }
      MessageBox *mb =
          MessageBox::Create(*this, "Failed to create instrument", MBBF_OK);
      DoModal(mb);
      return;
    }

    Trace::Log("INSTRUMENTIMPORT",
               "Created new instrument of type %d in slot %d", importedType,
               toInstrID_);
  } else {
    Trace::Log("INSTRUMENTIMPORT",
               "Keeping existing instrument of type %d in slot %d",
               importedType, toInstrID_);
  }

  Trace::Log("INSTRUMENTIMPORT", "Created new instrument of type %d",
             importedType);

  I_Instrument *instrument = bank->GetInstrument(toInstrID_);
  PersistencyResult result =
      PersistencyService::GetInstance()->ImportInstrument(instrument,
                                                          instrumentPath);

  Trace::Log("INSTRUMENTIMPORT", "Imported TYPE: %d", instrument->GetType());

  if (result == PERSIST_LOADED) {
    instrument->SetChanged();
    instrument->NotifyObservers();

    Variable *channelVar =
        instrument->FindVariable(FourCC::MidiInstrumentChannel);
    if (channelVar) {
      Trace::Log("INSTRUMENTIMPORT", "Final MIDI channel: %d",
                 channelVar->GetInt());
    }

    Trace::Log("INSTRUMENTIMPORT",
               "Final instrument type in importInstrument: %d",
               instrument->GetType());

    viewData_->currentInstrumentID_ = toInstrID_;
    Trace::Log("INSTRUMENTIMPORT",
               "Updated viewData_ currentInstrumentID_ to: %d", toInstrID_);

    MessageBox *mb = MessageBox::Create(*this, "Import successful", MBBF_OK);
    DoModal(mb,
            ModalViewCallback::create<InstrumentImportView,
                                      &InstrumentImportView::onImportSuccess>(
                *this));
  } else {
    if (importedType == IT_MSAMPLER) {
      removeImportedPreset(importedDecentSamplerPath.c_str());
    }
    MessageBox *mb = MessageBox::Create(*this, "Import failed", MBBF_OK);
    DoModal(mb);
  }

  isDirty_ = true;
}

bool InstrumentImportView::startDslibraryImport(const char *archivePath) {
  const size_t pathLength = std::strlen(archivePath);
  constexpr char extension[] = ".dslibrary";
  constexpr size_t extensionLength = sizeof(extension) - 1;
  if (pathLength <= extensionLength ||
      strcasecmp(archivePath + pathLength - extensionLength, extension) != 0) {
    return false;
  }
  if (Player::GetInstance()->IsRunning()) {
    MessageBox *mb = MessageBox::Create(
        *this, "cannot import dslibrary while sequencer running", MBBF_OK);
    DoModal(mb);
    return true;
  }
  Player::GetInstance()->StopStreaming();
  FileSystemPath::PathString destination;
  destination.assign(archivePath, pathLength - extensionLength);
  if (destination.is_truncated()) {
    MessageBox *mb =
        MessageBox::Create(*this, "Destination path too long", MBBF_OK);
    DoModal(mb);
    return true;
  }
  auto *fs = FileSystem::GetInstance();
  if (fs->exists(destination.c_str())) {
    MessageBox *mb =
        MessageBox::Create(*this, "Destination already exists", MBBF_OK);
    DoModal(mb);
    return true;
  }
  if (!DslibraryExtractor::StartAsync(archivePath, destination.c_str())) {
    MessageBox *mb = MessageBox::Create(
        *this, "Could not start library extraction", MBBF_OK);
    DoModal(mb);
    return true;
  }
  dslibraryRoot_ = destination;
  extractingDslibrary_ = true;
  isDirty_ = true;
  return true;
}

void InstrumentImportView::onImportSuccess(View &, ModalView &dialog) {
  if (dialog.GetReturnCode() != MBL_OK) {
    return;
  }

  Trace::Log("INSTRUMENTIMPORT",
             "Switching back to instrument view with ID: %d", toInstrID_);

  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(toInstrID_);
  if (instrument) {
    instrument->SetChanged();
    instrument->NotifyObservers();
  }

  ViewType vt = VT_INSTRUMENT;
  ViewEvent ve(VET_SWITCH_VIEW, &vt);
  SetChanged();
  NotifyObservers(&ve);
}

void InstrumentImportView::enterSysexFile(const char *path) {
  const fm6::Dx7BankParseResult result = readAndValidateSysexFile(path);
  Trace::Log("DX7PATCHIMPORT", "Open %s result %d", path,
             static_cast<int>(result));
  if (result != fm6::Dx7BankParseResult::Ok) {
    MessageBox *dialog =
        MessageBox::Create(*this, parseResultMessage(result), MBBF_OK);
    DoModal(dialog);
    return;
  }

  // List the staged bank's 32 patches as a virtual directory.
  fileEntries_.clear();
  FileSystemEntry parent;
  parent.name = kSysexBackName;
  parent.type = PFT_DIR;
  fileEntries_.push_back(parent);
  fm6::Dx7VoiceName voiceName{};
  for (size_t patchIndex = 0; patchIndex < fm6::Dx7BankPatchCount;
       ++patchIndex) {
    fm6::ExtractDx7VoiceName(stagedVoiceRecord(patchIndex), patchIndex,
                             voiceName);
    FileSystemEntry entry;
    char name[16];
    npf_snprintf(name, sizeof(name), "%02X %s",
                 static_cast<unsigned>(patchIndex + 1), voiceName.data());
    entry.name = name;
    entry.type = PFT_FILE;
    fileEntries_.push_back(entry);
  }
  browsingSysex_ = true;
  currentIndex_ = 0;
  topIndex_ = 0;
  isDirty_ = true;
}

void InstrumentImportView::leaveSysexListing() {
  stopPatchPreview();
  browsingSysex_ = false;
  setCurrentFolder(FileSystem::GetInstance(), currentDir_.c_str());
}

void InstrumentImportView::commitSysexPatch(size_t patchIndex) {
  stopPatchPreview();
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(toInstrID_);
  if (instrument == nullptr || instrument->GetType() != IT_FM6) {
    MessageBox *dialog =
        MessageBox::Create(*this, "Not a FM6 instrument", MBBF_OK);
    DoModal(dialog);
    return;
  }
  FM6Instrument *fm6Instrument = static_cast<FM6Instrument *>(instrument);
  fm6Instrument->ImportDx7Patch(stagedVoiceRecord(patchIndex));
  fm6::Dx7VoiceName voiceName{};
  fm6::ExtractDx7VoiceName(stagedVoiceRecord(patchIndex), patchIndex,
                           voiceName);
  fm6Instrument->SetName(voiceName.data());
  ViewType viewType = VT_INSTRUMENT;
  ViewEvent event(VET_SWITCH_VIEW, &viewType);
  SetChanged();
  NotifyObservers(&event);
}

void InstrumentImportView::startPatchPreview(const uint8_t *record) {
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(toInstrID_);
  if (instrument == nullptr || instrument->GetType() != IT_FM6 ||
      viewData_->song_ == nullptr) {
    return;
  }
  if (previewKeyHeld_) {
    stopPatchPreview();
  }
  const int channelCount = SONG_CHANNEL_COUNT;
  if (channelCount <= 0) {
    return;
  }
  static_cast<FM6Instrument *>(instrument)->SetPreviewPatch(record);
  previewChannel_ = static_cast<unsigned short>(channelCount - 1);
  Player::GetInstance()->PlayNote(static_cast<unsigned short>(toInstrID_),
                                  previewChannel_, 60, 0x7F);
  previewKeyHeld_ = true;
}

void InstrumentImportView::stopPatchPreview() {
  if (!previewKeyHeld_) {
    return;
  }
  Player::GetInstance()->StopNote(static_cast<unsigned short>(toInstrID_),
                                  previewChannel_);
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(toInstrID_);
  if (instrument != nullptr && instrument->GetType() == IT_FM6) {
    static_cast<FM6Instrument *>(instrument)->ClearPreviewPatch();
  }
  previewKeyHeld_ = false;
}

void InstrumentImportView::setCurrentFolder(FileSystem *fs, const char *name) {
  if (fs == nullptr || name == nullptr) {
    return;
  }

  const bool sameChooserDirectory = dslibraryPresetChooser_ &&
                                    FileSystemPath::IsAbsolutePath(name) &&
                                    std::strcmp(name, currentDir_.c_str()) == 0;
  if (!sameChooserDirectory) {
    listingOffset_ = 0;
  }
  const bool importingDx7Patch = mode_ != Mode::Instrument;
  const char *rootDirectory =
      importingDx7Patch ? "/"
                        : (dslibraryPresetChooser_ ? dslibraryRoot_.c_str()
                                                   : INSTRUMENTS_DIR);
  FileSystemPath::PathString nextDir;
  if (std::strcmp(name, BrowserPathUtils::kParentDirectoryName) == 0) {
    if (!BrowserPathUtils::ResolveParentDirectory(currentDir_.c_str(),
                                                  rootDirectory, nextDir)) {
      return;
    }
  } else if (FileSystemPath::IsAbsolutePath(name)) {
    if (!FileSystemPath::NormalizePath(name, nextDir)) {
      Trace::Error("INSTRUMENTIMPORT: Failed to normalize %s", name);
      return;
    }
  } else if (!FileSystemPath::JoinPath(currentDir_.c_str(), name, nextDir)) {
    Trace::Error("INSTRUMENTIMPORT: Failed to resolve %s from %s", name,
                 currentDir_.c_str());
    return;
  }

  fileEntries_.clear();
  const char *filter = importingDx7Patch
                           ? DX7_BANK_FILE_EXTENSION
                           : (dslibraryPresetChooser_ ? ".dspreset" : nullptr);
  bool listed = false;
  if (dslibraryPresetChooser_) {
    dslibraryPageEntries.clear();
    listed =
        fs->ListDirectoryPage(nextDir.c_str(), &dslibraryPageEntries, filter,
                              false, false, listingOffset_, listingTotal_);
    for (const auto &entry : dslibraryPageEntries) {
      fileEntries_.push_back(entry);
    }
  } else {
    listingTotal_ = 0;
    listed = fs->ListDirectory(nextDir.c_str(), &fileEntries_, filter, false);
  }
  const bool truncated = isDirectoryListTruncated(fs);
  if (!listed && !truncated) {
    Trace::Error("INSTRUMENTIMPORT: Failed to list %s", nextDir.c_str());
    return;
  }
  if (truncated) {
    Trace::Error("INSTRUMENTIMPORT: Truncated listing for %s", nextDir.c_str());
    showDirectoryListTruncatedDialog(*this);
  }

  if (dslibraryPresetChooser_) {
    for (auto entry = fileEntries_.begin(); entry != fileEntries_.end();) {
      if (entry->type != PFT_FILE) {
        ++entry;
        continue;
      }
      const char *name = entry->name.c_str();
      const size_t length = entry->name.size();
      constexpr char presetExt[] = ".dspreset";
      constexpr size_t presetExtLength = sizeof(presetExt) - 1;
      if (length <= presetExtLength ||
          strcasecmp(name + length - presetExtLength, presetExt) != 0) {
        entry = fileEntries_.erase(entry);
      } else {
        ++entry;
      }
    }
  }

  if (!dslibraryPresetChooser_ && lastRecoveryScanPath_ != nextDir) {
    lastRecoveryScanPath_ = nextDir;
    size_t recoveryOffset = 0;
    size_t recoveryTotal = 0;
    bool recoveryPromptShown = false;
    do {
      dslibraryPageEntries.clear();
      if (!fs->ListDirectoryPage(nextDir.c_str(), &dslibraryPageEntries,
                                 nullptr, true, false, recoveryOffset,
                                 recoveryTotal)) {
        break;
      }
      for (const auto &entry : dslibraryPageEntries) {
        FileSystemPath::PathString stagePath, markerPath;
        if (!FileSystemPath::JoinPath(nextDir.c_str(), entry.name.c_str(),
                                      stagePath) ||
            !FileSystemPath::JoinPath(stagePath.c_str(),
                                      ".dslibrary-incomplete", markerPath) ||
            !fs->exists(markerPath.c_str())) {
          continue;
        }
        pendingCleanupStage_ = stagePath;
        MessageBox *mb =
            MessageBox::Create(*this, "Incomplete library extraction found",
                               "Clean it up?", MBBF_YES | MBBF_NO);
        DoModal(mb,
                ModalViewCallback::create<
                    InstrumentImportView,
                    &InstrumentImportView::onCleanupIncompleteStage>(*this));
        recoveryPromptShown = true;
        break;
      }
      if (recoveryPromptShown || dslibraryPageEntries.empty()) {
        break;
      }
      recoveryOffset += dslibraryPageEntries.size();
    } while (recoveryOffset < recoveryTotal);
  }

  SortBrowserEntries(fileEntries_);

  currentDir_ = nextDir;
  currentIndex_ = 0;
  topIndex_ = 0;
  isDirty_ = true;
  if (!BrowserPathUtils::PrependParentEntry(currentDir_.c_str(), rootDirectory,
                                            &fileEntries_) &&
      mode_ == Mode::Dx7Patch) {
    // At the filesystem root ".." leads back to the FM6 preset browser (this
    // mode is only ever entered from there).
    if (fileEntries_.full()) {
      fileEntries_.pop_back();
    }
    FileSystemEntry parentEntry;
    parentEntry.name = "..";
    parentEntry.type = PFT_DIR;
    parentEntry.size = 0;
    parentEntry.isHidden = false;
    fileEntries_.insert(fileEntries_.begin(), parentEntry);
  }
  Trace::Debug("loaded %d files from %s", fileEntries_.size(),
               currentDir_.c_str());
}

void InstrumentImportView::onCleanupIncompleteStage(View &, ModalView &dialog) {
  const int selection = dialog.GetReturnCode();
  if (selection == MBL_YES && !DslibraryExtractor::CleanupIncompleteStage(
                                  pendingCleanupStage_.c_str())) {
    MessageBox *mb = MessageBox::Create(
        *this, "Could not clean incomplete extraction", MBBF_OK);
    DoModal(mb);
  }
  pendingCleanupStage_.clear();
}
