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

#include "SampleEditorView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Instruments/WavFileWriter.h"
#include "Application/Instruments/WavHeader.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Foundation/Types/Types.h"
#include "ModalDialogs/MessageBox.h"
#include "SampleEditProgressDisplay.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include "System/Profiler/Profiler.h"
#include "UIController.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

SampleEditProgressDisplay *sampleEditProgressDisplay = nullptr;

void wavProgressCallback(uint8_t percent) {
  if (sampleEditProgressDisplay) {
    sampleEditProgressDisplay->Update(percent);
  }
}

// Initialize static member
ViewType SampleEditorView::sourceViewType_ = VT_SONG;

constexpr const char *const sampleEditOperationNames[] = {"Trim",
                                                          "Peak Normalize"};
constexpr uint32_t sampleEditOperationCount =
    sizeof(sampleEditOperationNames) / sizeof(sampleEditOperationNames[0]);
constexpr etl::string_view kSampleEditTempLeafName = ".sampledit_tmp.wav";
constexpr etl::string_view kSampleEditBackupLeafName = ".sampledit_bak.wav";

constexpr int32_t GraphXOffset = GraphField::DefaultXOffset;
constexpr int32_t GraphYOffset = 2 * CHAR_HEIGHT;

namespace {

bool extractLeafName(const char *path,
                     etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &leafName) {
  FileSystemPath::PathString baseName;
  if (!FileSystemPath::BaseName(path, baseName)) {
    leafName.clear();
    return false;
  }

  leafName = baseName.c_str();
  return !leafName.is_truncated() && !leafName.empty();
}

bool extractParentPath(const char *path, etl::string<PFILENAME_SIZE> &parent) {
  FileSystemPath::PathString dirName;
  if (!FileSystemPath::DirName(path, dirName)) {
    parent.clear();
    return false;
  }

  parent = dirName.c_str();
  return !parent.empty();
}

bool pathIsWithinDirectory(const char *path, const char *directory) {
  FileSystemPath::PathString normalizedPath;
  FileSystemPath::PathString normalizedDir;
  return FileSystemPath::NormalizePath(path, normalizedPath) &&
         FileSystemPath::NormalizePath(directory, normalizedDir) &&
         (normalizedPath == normalizedDir ||
          FileSystemPath::IsParentOf(normalizedDir.c_str(),
                                     normalizedPath.c_str()));
}

} // namespace

GraphicsLayout SampleEditorView::GetGraphicsLayout() {
  static constexpr GraphicsRegion regions[] = {
      {GraphXOffset, GraphYOffset, 660, 180}};
  return {regions, 1};
}

SampleEditorView::SampleEditorView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), fullWaveformRedraw_(false), isPlaying_(false),
      isSingleCycle_(false), playKeyHeld_(false), playbackPosition_(0.0f),
      playbackStartFrame_(0), lastAnimationTime_(0),
      sys_(System::GetInstance()), startVar_(FourCC::VarSampleEditStart, 0),
      endVar_(FourCC::VarSampleEditEnd, 0),
      // bit of a hack to use InstrumentName but we never actually persist this
      // in any config file here
      filenameVar_(FourCC::InstrumentName, ""),
      operationVar_(FourCC::VarSampleEditOperation, sampleEditOperationNames,
                    sampleEditOperationCount,
                    static_cast<int>(SampleEditOperation::Trim)),
      win(w), graphFieldPos_(GraphXOffset, GraphYOffset),
      graphField_(graphFieldPos_, GraphField::BitmapWidth,
                  GraphField::BitmapHeight) {
  assignWorkingFilename();
  graphField_.SetShowBaseline(true);
  graphField_.SetBorderColors(CD_HILITE1, CD_HILITE2);
}

SampleEditorView::~SampleEditorView() {}

void SampleEditorView::Reset() {
  fullWaveformRedraw_ = false;
  isPlaying_ = false;
  isSingleCycle_ = false;
  playKeyHeld_ = false;
  playbackPosition_ = 0.0f;
  playbackStartFrame_ = 0;
  lastAnimationTime_ = 0;
  start_ = 0;
  end_ = 0;
  tempSampleSize_ = 0;
  headerInfo_ = WavHeaderInfo{};
  headerValid_ = false;

  filename.clear();
  filenameVar_.SetString("", false);
  workingFilename_.clear();
  hasWorkingCopy_ = false;
  graphField_.Reset();
  graphField_.SetShowBaseline(true);
  graphField_.SetBorderColors(CD_HILITE1, CD_HILITE2);
  selectedMarker_ = MarkerStart;

  fieldList_.clear();
  bigHexVarField_.clear();
  intVarField_.clear();
  actionField_.clear();
  nameTextField_.clear();
}

// Static method to set the source view type before opening SampleEditorView
void SampleEditorView::SetSourceViewType(ViewType vt) { sourceViewType_ = vt; }

void SampleEditorView::assignWorkingFilename() {
  if (!buildSiblingPath(viewData_->sampleEditorFilename,
                        kSampleEditTempLeafName, workingFilename_)) {
    Trace::Error("SampleEditorView: Failed creating temp filename from %s",
                 viewData_->sampleEditorFilename.c_str());
  }
}

bool SampleEditorView::buildSiblingPath(
    const etl::string<PFILENAME_SIZE> &srcFilename,
    etl::string_view siblingName,
    etl::string<PFILENAME_SIZE> &outFilename) const {
  outFilename.clear();
  if (srcFilename.empty() || siblingName.empty() ||
      !FileSystemPath::IsAbsolutePath(srcFilename.c_str())) {
    return false;
  }

  etl::string<PFILENAME_SIZE> parentPath;
  if (!extractParentPath(srcFilename.c_str(), parentPath)) {
    return false;
  }

  etl::string<PFILENAME_SIZE> combined(parentPath);
  if (combined.empty()) {
    return false;
  }
  if (combined.size() > 1) {
    combined.append("/");
  }
  combined.append(siblingName.data(), siblingName.size());

  FileSystemPath::PathString normalizedPath;
  if (combined.is_truncated() ||
      !FileSystemPath::NormalizePath(combined.c_str(), normalizedPath)) {
    return false;
  }

  outFilename = normalizedPath.c_str();
  return !outFilename.is_truncated();
}

const etl::string<PFILENAME_SIZE> &SampleEditorView::activeFilename() const {
  return hasWorkingCopy_ ? workingFilename_ : viewData_->sampleEditorFilename;
}

bool SampleEditorView::ensureWorkingCopy() {
  auto fs = FileSystem::GetInstance();

  if (hasWorkingCopy_) {
    return true;
  }

  if (viewData_->sampleEditorFilename.empty()) {
    Trace::Error("SampleEditorView: No source file available");
    return false;
  }

  if (workingFilename_.empty()) {
    assignWorkingFilename();
  }
  if (workingFilename_.empty()) {
    Trace::Error("SampleEditorView: Could not prepare temp filename");
    return false;
  }
  if (workingFilename_ == viewData_->sampleEditorFilename) {
    Trace::Error(
        "SampleEditorView: Temp filename conflicts with source file %s",
        workingFilename_.c_str());
    return false;
  }

  if (fs->exists(workingFilename_.c_str()) &&
      !fs->DeleteFile(workingFilename_.c_str())) {
    Trace::Error("SampleEditorView: Failed deleting stale temp file %s",
                 workingFilename_.c_str());
    return false;
  }

  if (!fs->CopyFile(viewData_->sampleEditorFilename.c_str(),
                    workingFilename_.c_str())) {
    Trace::Error("SampleEditorView: Failed creating temp copy %s -> %s",
                 viewData_->sampleEditorFilename.c_str(),
                 workingFilename_.c_str());
    return false;
  }

  hasWorkingCopy_ = true;
  return true;
}

void SampleEditorView::discardWorkingCopy() {
  auto fs = FileSystem::GetInstance();
  if (!fs || workingFilename_.empty()) {
    hasWorkingCopy_ = false;
    return;
  }

  if (fs->exists(workingFilename_.c_str()) &&
      !fs->DeleteFile(workingFilename_.c_str())) {
    Trace::Error("SampleEditorView: Failed deleting temp file %s",
                 workingFilename_.c_str());
  }

  hasWorkingCopy_ = false;
}

bool SampleEditorView::commitWorkingCopy(
    const etl::string<PFILENAME_SIZE> &targetFilename) {
  auto fs = FileSystem::GetInstance();

  if (!hasWorkingCopy_) {
    return true;
  }

  const bool commitToOriginal =
      (targetFilename == viewData_->sampleEditorFilename);
  if (commitToOriginal) {
    etl::string<PFILENAME_SIZE> backupFilename;
    if (!buildSiblingPath(targetFilename, kSampleEditBackupLeafName,
                          backupFilename)) {
      Trace::Error("SampleEditorView: Failed creating backup filename for %s",
                   targetFilename.c_str());
      return false;
    }
    if (backupFilename == targetFilename ||
        backupFilename == workingFilename_) {
      Trace::Error("SampleEditorView: Backup filename conflicts for %s",
                   targetFilename.c_str());
      return false;
    }
    if (fs->exists(backupFilename.c_str()) &&
        !fs->DeleteFile(backupFilename.c_str())) {
      Trace::Error("SampleEditorView: Failed deleting stale backup file %s",
                   backupFilename.c_str());
      return false;
    }

    bool movedOriginalToBackup = false;
    if (fs->exists(targetFilename.c_str())) {
      if (!fs->MoveFile(targetFilename.c_str(), backupFilename.c_str())) {
        Trace::Error(
            "SampleEditorView: Failed moving original file to backup %s"
            " -> %s",
            targetFilename.c_str(), backupFilename.c_str());
        return false;
      }
      movedOriginalToBackup = true;
    }

    if (!fs->MoveFile(workingFilename_.c_str(), targetFilename.c_str())) {
      Trace::Error("SampleEditorView: Failed moving temp file %s -> %s",
                   workingFilename_.c_str(), targetFilename.c_str());
      if (movedOriginalToBackup &&
          !fs->MoveFile(backupFilename.c_str(), targetFilename.c_str())) {
        Trace::Error("SampleEditorView: Failed restoring original file from %s",
                     backupFilename.c_str());
      }
      return false;
    }

    if (movedOriginalToBackup && fs->exists(backupFilename.c_str()) &&
        !fs->DeleteFile(backupFilename.c_str())) {
      Trace::Error("SampleEditorView: Failed deleting backup file %s",
                   backupFilename.c_str());
    }
    hasWorkingCopy_ = false;
    return true;
  } else {
    if (fs->exists(targetFilename.c_str()) &&
        !fs->DeleteFile(targetFilename.c_str())) {
      Trace::Error("SampleEditorView: Failed deleting destination file %s",
                   targetFilename.c_str());
      return false;
    }

    if (!fs->CopyFile(workingFilename_.c_str(), targetFilename.c_str())) {
      Trace::Error("SampleEditorView: Failed committing temp file %s -> %s",
                   workingFilename_.c_str(), targetFilename.c_str());
      return false;
    }
  }

  discardWorkingCopy();
  return true;
}

void SampleEditorView::OnFocus() {
  assignWorkingFilename();
  discardWorkingCopy();

  const auto newSampleFile = viewData_->sampleEditorFilename;
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> displayFilename;
  if (!extractLeafName(newSampleFile.c_str(), displayFilename)) {
    Trace::Error("SampleEditorView: Failed to extract display name from %s",
                 newSampleFile.c_str());
    filenameVar_.SetString("", false);
  } else if (displayFilename.size() >= 4 &&
             strcmp(displayFilename.c_str() + displayFilename.size() - 4,
                    ".wav") == 0) {
    filenameVar_.SetString(
        displayFilename.substr(0, displayFilename.size() - 4).c_str(), false);
  } else {
    filenameVar_.SetString(displayFilename.c_str(), false);
  }

  // Load the sample using its absolute filename and fill the wave data cache.
  loadSample(newSampleFile);

  // Update cached sample parameters
  updateSampleParameters();
  updateZoomWindow();
  selectedMarker_ = MarkerStart;

  // Force redraw of waveform
  fullWaveformRedraw_ = true;

  addAllFields();
}

void SampleEditorView::addAllFields() {
  // We currently have no way to update fields with the variable they are
  // assigned so instead we need to first clear out all the previous fields
  // and then re-add them just like we do on the InstrumentView
  fieldList_.clear();
  bigHexVarField_.clear();
  intVarField_.clear();
  actionField_.clear();
  nameTextField_.clear();
  // no need to clear staticField_  as its not added to fieldList_

  GUIPoint position = GetAnchor();

  position._y = 9; // offset enough for waveform display
  position._x = 5;

  auto label =
      etl::make_string_with_capacity<MAX_UITEXTFIELD_LABEL_LENGTH>("name: ");

  auto defaultRecName =
      etl::make_string_with_capacity<MAX_INSTRUMENT_NAME_LENGTH>(
          RECORDING_FILENAME)
          .substr(0, strlen(RECORDING_FILENAME) - 4);

  nameTextField_.emplace_back(filenameVar_, position, label,
                              FourCC::InstrumentName, defaultRecName);
  fieldList_.insert(fieldList_.end(), &(*nameTextField_.rbegin()));

  const uint16_t baseX = position._x;
  const uint32_t maxSampleIndex = tempSampleSize_ > 0 ? tempSampleSize_ - 1 : 0;

  position._y += 1;
  bigHexVarField_.emplace_back(position, startVar_, 7, "Start: %7.7X", 0,
                               maxSampleIndex, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));
  (*bigHexVarField_.rbegin()).AddObserver(*this);

  // Add end position control
  position._y += 1;
  bigHexVarField_.emplace_back(position, endVar_, 7, "End: %7.7X", 0,
                               maxSampleIndex, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));
  (*bigHexVarField_.rbegin()).AddObserver(*this);

  // Operation selector
  position._y += 1;
  position._x = baseX;
  uint8_t maxOperationIndex =
      operationVar_.GetListSize() > 0 ? operationVar_.GetListSize() - 1 : 0;
  intVarField_.emplace_back(position, operationVar_, "Op: %s", 0,
                            maxOperationIndex, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  // Apply button
  position._x = baseX + 20;
  actionField_.emplace_back("Apply", FourCC::ActionOK, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  // Save button row
  position._y += 2; // want extra empty row between these buttons & prev Apply
  position._x = baseX;
  actionField_.emplace_back("Save", FourCC::ActionSave, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  // load & save button
  position._x += 5;
  if (!viewData_->isShowingSampleEditorProjectPool) {
    actionField_.emplace_back("Save&Load", FourCC::ActionLoadAndSave, position);
    fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
    (*actionField_.rbegin()).AddObserver(*this);
    position._x += 12;
  } else {
    position._x += 7;
  }

  // discard button
  actionField_.emplace_back("Discard", FourCC::ActionCancel, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  fieldList_.insert(fieldList_.end(), &graphField_);
}

void SampleEditorView::ProcessButtonMask(unsigned short mask, bool pressed) {
  updateSelectedMarkerFromFocus();

  // Check for key release events
  if (!pressed) {
    // Check if play key was released (exactly like ImportView approach)
    if (playKeyHeld_ && !(mask & EPBM_PLAY)) {
      // Play key no longer pressed so should stop playback
      playKeyHeld_ = false;

      if (Player::GetInstance()->IsPlaying()) {
        // Stop playback regardless of whether it's regular or looping
        Player::GetInstance()->StopStreaming();
        isPlaying_ = false;
        isDirty_ = true;
      }
      return;
    }
  }

  // If not pressed, let parent handle it and return
  if (!pressed) {
    FieldView::ProcessButtonMask(mask, pressed);
    return;
  }

  if (mask & EPBM_NAV) {
    if (mask & EPBM_LEFT) {
      ViewType vt = SampleEditorView::sourceViewType_;
      navigateToView(vt);
      return;
    }
    // For other NAV combinations, let parent handle it
    FieldView::ProcessButtonMask(mask, pressed);
    return;
  } else if (mask & EPBM_PLAY) {
    // Set flag to track that play key is being held down (like in ImportView)
    playKeyHeld_ = true;

    // Start sample playback if no already playing
    if (!isPlaying_) {
      // Get the sample file name
      const auto &sampleFileName = activeFilename();
      isSingleCycle_ = (headerValid_ && headerInfo_.numChannels == 1 &&
                        tempSampleSize_ <= SINGLE_CYCLE_MAX_SAMPLE_SIZE);

      Trace::Debug("DEBUG: Starting playback of sample '%s' (size=%d, "
                   "singleCycle=%s)\n",
                   sampleFileName.c_str(), tempSampleSize_,
                   isSingleCycle_ ? "true" : "false");

      // Reset playback state
      isPlaying_ = true;

      // Get the start position which is where playback will begin
      uint32_t startSample = startVar_.GetInt();
      if (startSample < tempSampleSize_) {
        // Initialize normalized playback position (0.0 - 1.0)
        playbackPosition_ = (float)startSample / tempSampleSize_;
      } else {
        playbackPosition_ = 0.0f;
      }

      // Store the current animation frame as our start frame
      playbackStartFrame_ = AppWindow::GetAnimationFrameCounter();
      lastAnimationTime_ = sys_->Millis();

      // If something is already playing, stop it first
      if (Player::GetInstance()->IsPlaying()) {
        Player::GetInstance()->StopStreaming();
      }

      // Start playing the sample with just the filename
      if (isSingleCycle_) {
        Player::GetInstance()->StartLoopingStreaming(sampleFileName.c_str());
      } else {
        // Start playback from the specified start position
        Player::GetInstance()->StartStreaming(sampleFileName.c_str(),
                                              startSample);
        isDirty_ = true;
      }
    }
    return;
  }

  bool graphFocused = (GetFocus() == &graphField_);
  if (graphFocused && (mask & EPBM_EDIT)) {
    if (mask & EPBM_LEFT) {
      selectedMarker_ = MarkerStart;
      isDirty_ = true;
      ((AppWindow &)w_).SetDirty();
      return;
    }
    if (mask & EPBM_RIGHT) {
      selectedMarker_ = MarkerEnd;
      isDirty_ = true;
      ((AppWindow &)w_).SetDirty();
      return;
    }
  }

  if (graphFocused && (mask & EPBM_ENTER)) {
    uint32_t viewStart = graphField_.ViewStart();
    uint32_t viewEnd = graphField_.ViewEnd();
    uint32_t viewSpan = (viewEnd > viewStart) ? (viewEnd - viewStart) : 0;
    if (viewSpan == 0) {
      updateZoomWindow();
      viewStart = graphField_.ViewStart();
      viewEnd = graphField_.ViewEnd();
      viewSpan = (viewEnd > viewStart) ? (viewEnd - viewStart) : 0;
    }
    if (viewSpan > 0) {
      int32_t delta = 0;
      if (mask & (EPBM_LEFT | EPBM_RIGHT)) {
        delta = static_cast<int32_t>(std::max<uint32_t>(1, viewSpan / 64));
        if (mask & EPBM_LEFT) {
          delta = -delta;
        }
      } else if (mask & (EPBM_UP | EPBM_DOWN)) {
        delta = static_cast<int32_t>(std::max<uint32_t>(1, viewSpan / 16));
        if (mask & EPBM_DOWN) {
          delta = -delta;
        }
      }
      if (delta != 0) {
        if (selectedMarker_ == MarkerStart) {
          int32_t start = startVar_.GetInt();
          int32_t newStart = start + delta;
          if (newStart < 0) {
            newStart = 0;
          }
          int32_t maxStart = (tempSampleSize_ > 0)
                                 ? static_cast<int32_t>(tempSampleSize_ - 1)
                                 : 0;
          if (newStart > maxStart) {
            newStart = maxStart;
          }
          int32_t end = endVar_.GetInt();
          if (newStart > end) {
            newStart = end;
          }
          startVar_.SetInt(newStart);
        } else {
          int32_t end = endVar_.GetInt();
          int32_t newEnd = end + delta;
          if (newEnd < 0) {
            newEnd = 0;
          }
          int32_t maxEnd = (tempSampleSize_ > 0)
                               ? static_cast<int32_t>(tempSampleSize_ - 1)
                               : 0;
          if (newEnd > maxEnd) {
            newEnd = maxEnd;
          }
          int32_t start = startVar_.GetInt();
          if (newEnd < start) {
            newEnd = start;
          }
          endVar_.SetInt(newEnd);
        }
        updateSampleParameters();
        isDirty_ = true;
        ((AppWindow &)w_).SetDirty();
        return;
      }
    }
  }

  // We allow zooming from any place of the screen
  if ((mask & EPBM_EDIT) && (mask & EPBM_UP)) {
    adjustZoom(1);
    return;
  }
  if ((mask & EPBM_EDIT) && (mask & EPBM_DOWN)) {
    adjustZoom(-1);
    return;
  }

  // For all other button presses, let the parent class handle navigation
  FieldView::ProcessButtonMask(mask, pressed);
  updateSelectedMarkerFromFocus();

  // EDIT Modifier
  if (mask & EPBM_EDIT) {
    if (mask & EPBM_ENTER) {
      UIField *focus = GetFocus();
      for (auto &field : bigHexVarField_) {
        if (&field == focus &&
            field.GetVariableID() == FourCC::VarSampleEditEnd) {
          Variable &var = field.GetVariable();
          var.SetInt(tempSampleSize_ - 1);
          isDirty_ = true;
          break;
        }
      };
    }
  }
}

void SampleEditorView::DrawView() {
  Clear();

  GUITextProperties props;

  setTitle(etl::string<23>("Sample Edit"));

  // Borders and graph columns now route beneath modals like other pixels.
  FieldView::Redraw();
  isDirty_ = true;
}

void SampleEditorView::DrawWaveForm() {
  // Marker updates continue into live backing while a modal covers the graph.
  if (fullWaveformRedraw_) {
    fullWaveformRedraw_ = false;
    graphField_.RequestFullRedraw();
  }
  if (!graphField_.WaveformValid()) {
    rebuildWaveform();
  }
  updateGraphMarkers();
  graphField_.DrawGraph(*this);
}

void SampleEditorView::updateSelectedMarkerFromFocus() {
  UIField *focus = GetFocus();
  for (auto &field : bigHexVarField_) {
    if (&field != focus) {
      continue;
    }
    if (field.GetVariableID() == FourCC::VarSampleEditStart) {
      selectedMarker_ = MarkerStart;
    } else if (field.GetVariableID() == FourCC::VarSampleEditEnd) {
      selectedMarker_ = MarkerEnd;
    }
    break;
  }
}

uint32_t SampleEditorView::selectionCenterSample() const {
  if (tempSampleSize_ == 0) {
    return 0;
  }
  uint32_t center = (selectedMarker_ == MarkerEnd) ? end_ : start_;
  if (center >= tempSampleSize_) {
    center = tempSampleSize_ - 1;
  }
  return center;
}

void SampleEditorView::updateZoomWindow() {
  if (tempSampleSize_ == 0) {
    graphField_.SetSampleSize(0);
    return;
  }
  graphField_.SetSampleSize(tempSampleSize_);
  if (graphField_.UpdateZoomWindow(selectionCenterSample())) {
    graphField_.InvalidateWaveform();
    graphField_.RequestFullRedraw();
  }
}

void SampleEditorView::adjustZoom(int32_t delta) {
  if (graphField_.AdjustZoom(delta, selectionCenterSample())) {
    graphField_.InvalidateWaveform();
    graphField_.RequestFullRedraw();
    isDirty_ = true;
    ((AppWindow &)w_).SetDirty();
  }
}

void SampleEditorView::updateGraphMarkers() {
  graphField_.SetMarkerCount(3);
  bool hasSample = tempSampleSize_ > 0;

  if (hasSample) {
    ColorDefinition startColor =
        (selectedMarker_ == MarkerStart) ? CD_HILITE2 : CD_ACCENT;
    ColorDefinition endColor =
        (selectedMarker_ == MarkerEnd) ? CD_HILITE2 : CD_ACCENT;
    graphField_.SetMarker(0, start_, startColor, true);
    graphField_.SetMarker(1, end_, endColor, true);
  } else {
    graphField_.SetMarker(0, 0, CD_ACCENT, false);
    graphField_.SetMarker(1, 0, CD_ACCENT, false);
  }

  uint32_t playheadSample = 0;
  bool playheadVisible = false;
  if (isPlaying_ && tempSampleSize_ > 0) {
    playheadSample = static_cast<uint32_t>(playbackPosition_ * tempSampleSize_);
    if (playheadSample >= tempSampleSize_) {
      playheadSample = tempSampleSize_ - 1;
    }
    playheadVisible = true;
  }
  graphField_.SetMarker(2, playheadSample, CD_NORMAL, playheadVisible);
}

void SampleEditorView::rebuildWaveform() {
  graphField_.InvalidateWaveform();
  graphField_.BeginRmsBuild();

  if (!headerValid_ || tempSampleSize_ == 0) {
    return;
  }
  if (graphField_.ViewEnd() <= graphField_.ViewStart()) {
    return;
  }

  auto file = FileSystem::GetInstance()->Open(activeFilename().c_str(), "r");
  if (!file) {
    Trace::Error("SampleEditorView: Failed to open file for waveform rebuild");
    return;
  }

  uint32_t bytesPerFrame = static_cast<uint32_t>(headerInfo_.numChannels) *
                           static_cast<uint32_t>(headerInfo_.bytesPerSample);
  if (bytesPerFrame == 0) {
    Trace::Error("SampleEditorView: Invalid WAV header for waveform rebuild");
    return;
  }

  uint32_t viewStart = graphField_.ViewStart();
  uint32_t viewEnd = graphField_.ViewEnd();
  uint32_t totalFrames = viewEnd - viewStart;
  uint64_t startOffset =
      headerInfo_.dataOffset + static_cast<uint64_t>(viewStart) * bytesPerFrame;
  file->Seek(static_cast<uint32_t>(startOffset), SEEK_SET);

  static constexpr uint32_t ChunkFrames = 512;
  uint32_t framesRemaining = totalFrames;
  uint32_t currentFrame = 0;
  while (framesRemaining > 0) {
    uint32_t framesToRead = std::min(ChunkFrames, framesRemaining);
    uint32_t bytesToRead = framesToRead * bytesPerFrame;
    uint32_t bytesRead = file->Read(chunkBuffer_, bytesToRead);
    uint32_t framesRead = (bytesPerFrame > 0) ? bytesRead / bytesPerFrame : 0;
    if (framesRead == 0) {
      break;
    }

    const uint8_t *byteBuffer = reinterpret_cast<const uint8_t *>(chunkBuffer_);
    for (uint32_t i = 0; i < framesRead; ++i) {
      uint32_t frameOffset = i * bytesPerFrame;
      int16_t sampleValue = 0;
      if (headerInfo_.bitsPerSample == 8) {
        if (frameOffset >= bytesRead) {
          break;
        }
        int16_t centered = static_cast<int16_t>(byteBuffer[frameOffset]) - 128;
        sampleValue = centered << 8;
      } else {
        const int16_t *frameSamples =
            reinterpret_cast<const int16_t *>(byteBuffer + frameOffset);
        sampleValue = frameSamples[0];
      }
      int16_t clampedSample = std::clamp<int16_t>(sampleValue, -32768, 32767);
      uint32_t sampleIndex = viewStart + currentFrame + i;
      graphField_.AccumulateRmsSample(sampleIndex, clampedSample);
    }

    currentFrame += framesRead;
    framesRemaining -= framesRead;

    if (bytesRead < bytesToRead) {
      break;
    }
  }

  graphField_.FinalizeRmsBuild();
}

void SampleEditorView::AnimationUpdate() {
  // Update playhead position if sample is playing
  if (isPlaying_) {
    // Check if the Player is still playing the sample
    if (!Player::GetInstance()->IsPlaying()) {
      // Playback has stopped (reached the end of non-looping sample)
      Player::GetInstance()->StopStreaming();
      isPlaying_ = false;
      playbackPosition_ = 0;
      // forceRedraw_ = true;
      Trace::Debug("DEBUG: Playback stopped, resetting playhead\n");
    } else {
      // Calculate the time elapsed since the last animation frame
      uint32_t currentTime = sys_->Millis();
      uint32_t elapsedTime = currentTime - lastAnimationTime_;
      lastAnimationTime_ = currentTime;

      // Get the sample duration in milliseconds, assuming 44.1kHz sample rate
      float durationMs = (float)tempSampleSize_ / 44100.0f * 1000.0f;

      // Calculate the normalized playback position increment
      float positionIncrement = (float)elapsedTime / durationMs;

      // Update the playback position
      playbackPosition_ += positionIncrement;

      // Calculate the position in the sample
      float samplePos = playbackPosition_ * tempSampleSize_;

      // Check if we've reached the end
      if (samplePos >= end_ || samplePos >= tempSampleSize_) {
        samplePos = end_;
        Trace::Debug("Playback stopped at end!");
      }

      // Update position (normalized to full sample range)
      playbackPosition_ = samplePos / tempSampleSize_;
    }
  }

  // Check if we need to update the waveform display
  DrawWaveForm();

  // Modal text has its own cache; restoration needs no two-frame graph repaint.

  GUITextProperties props;
  drawTitlebar(TEXT_REGION_HEADER, props);
  drawPowerButtonUI(props);
}

void SampleEditorView::Update(Observable &o, I_ObservableData *d) {
  // When any of our observed variables change, update the cached parameters
  updateSampleParameters();

  if (!hasFocus_) {
    return;
  }

  uintptr_t fourcc = (uintptr_t)d;

  switch (fourcc) {
  case FourCC::ActionOK: {
    // Stop playback if active before applying any destructive operation
    if (Player::GetInstance()->IsPlaying()) {
      Player::GetInstance()->StopStreaming();
    }
    isPlaying_ = false;
    playKeyHeld_ = false;

    auto opName = operationVar_.GetString();
    etl::string<SCREEN_WIDTH - 2> confirmLine("Apply ");
    confirmLine.append(opName.c_str());
    confirmLine.append("?");

    MessageBox *mb =
        MessageBox::Create(*this, confirmLine.c_str(), "Saved only after Save",
                           MBBF_YES | MBBF_NO);

    // The overlay captures the current waveform; do not erase it before
    // opening.

    DoModal(
        mb,
        ModalViewCallback::create<SampleEditorView,
                                  &SampleEditorView::onConfirmApplyOperation>(
            *this));
    return;
  }
  case FourCC::ActionSave: {
    attemptSave(false);
    return;
  }
  case FourCC::ActionLoadAndSave: {
    attemptSave(true);
    return;
  }
  case FourCC::ActionCancel: {
    discardWorkingCopy();

    const auto &originalFilename = viewData_->sampleEditorFilename;
    if (pathIsWithinDirectory(originalFilename.c_str(), RECORDINGS_DIR) &&
        !viewData_->isShowingSampleEditorProjectPool) {
      auto fs = FileSystem::GetInstance();
      if (fs) {
        if (!fs->DeleteFile(originalFilename.c_str())) {
          Trace::Error("SampleEditorView: Failed to discard recording %s",
                       originalFilename.c_str());
        }
      } else {
        Trace::Error("SampleEditorView: Failed to get FS to delete: %s",
                     originalFilename.c_str());
      }
    }
    ViewType vt = SampleEditorView::sourceViewType_;
    navigateToView(vt);
    return;
  }
  }
}

void SampleEditorView::onConfirmApplyOperation(View &, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    if (!applySelectedOperation()) {
      MessageBox *error =
          MessageBox::Create(*this, "Operation failed", MBBF_OK);
      DoModal(
          error,
          ModalViewCallback::create<SampleEditorView,
                                    &SampleEditorView::onOperationFailedAck>(
              *this));
    }
  }

  isDirty_ = true;
}

void SampleEditorView::onConfirmOverwriteSave(View &, ModalView &dialog) {
  const bool loadToPool = pendingOverwriteLoadToPool_;
  pendingOverwriteLoadToPool_ = false;

  if (dialog.GetReturnCode() == MBL_OK) {
    confirmSave(loadToPool);
    return;
  }

  isDirty_ = true;
}

void SampleEditorView::onOperationFailedAck(View &, ModalView &) {

  isDirty_ = true;
}

void SampleEditorView::onSimpleModalDismiss(View &, ModalView &) {

  isDirty_ = true;
}

bool SampleEditorView::applySelectedOperation() {
  updateSampleParameters();

  if (!viewData_) {
    Trace::Error("SampleEditorView: View data unavailable");
    return false;
  }

  uint8_t opIndex = operationVar_.GetInt();
  if (opIndex < 0 ||
      opIndex > static_cast<int>(SampleEditOperation::Normalize)) {
    Trace::Error("SampleEditorView: Invalid operation index %d", opIndex);
    return false;
  }

  auto op = static_cast<SampleEditOperation>(opIndex);
  SampleEditProgressDisplay progressDisplay(viewData_->sampleEditorFilename);
  progressDisplay.Update(0);

  if (!ensureWorkingCopy()) {
    progressDisplay.Finish(false);
    return false;
  }

  switch (op) {
  case SampleEditOperation::Trim: {
    return applyTrimOperation(static_cast<uint32_t>(start_),
                              static_cast<uint32_t>(end_));
  }
  case SampleEditOperation::Normalize: {
    return applyNormalizeOperation();
  }
  default:
    Trace::Error("SampleEditorView: Unsupported operation %d", opIndex);
    break;
  }
  return false;
}

bool SampleEditorView::applyTrimOperation(uint32_t start_, uint32_t end_) {
  if (Player::GetInstance()->IsPlaying()) {
    Player::GetInstance()->StopStreaming();
  }
  isPlaying_ = false;
  playKeyHeld_ = false;

  if (!viewData_) {
    Trace::Error("SampleEditorView: View data unavailable");
    return false;
  }

  const auto &workingFilename = activeFilename();
  if (workingFilename.empty()) {
    Trace::Error("SampleEditorView: No filename available for trim");
    return false;
  }
  const auto &displayFilename = viewData_->sampleEditorFilename;

  if (tempSampleSize_ == 0) {
    Trace::Error("SampleEditorView: Cannot trim empty sample");
    return false;
  }

  if (end_ < start_) {
    Trace::Error("SampleEditorView: Trim range invalid (%u < %u)", end_,
                 start_);
    return false;
  }

  SampleEditProgressDisplay progressDisplay(displayFilename);
  WavTrimResult trimResult{};
  sampleEditProgressDisplay = &progressDisplay;
  bool trimmed = WavFileWriter::TrimFile(
      workingFilename.c_str(), start_, end_, static_cast<void *>(chunkBuffer_),
      sizeof(chunkBuffer_), trimResult, wavProgressCallback);
  sampleEditProgressDisplay = nullptr;
  progressDisplay.Finish(trimmed);
  if (!trimmed) {
    return false;
  }

  if (!trimResult.trimmed) {
    startVar_.SetInt(0);
    endVar_.SetInt(trimResult.totalFrames > 0
                       ? static_cast<int>(trimResult.totalFrames - 1)
                       : 0);
    updateSampleParameters();
    fullWaveformRedraw_ = true;
    Trace::Log("SAMPLEEDITOR",
               "Trim skipped because selection spans entire sample");
    return true;
  }

  loadSample(activeFilename());
  if (tempSampleSize_ == 0 || !headerValid_) {
    Trace::Error("SampleEditorView: Failed to reload trimmed sample %s",
                 activeFilename().c_str());
    return false;
  }

  uint32_t refreshedSize = static_cast<uint32_t>(tempSampleSize_);
  uint32_t newEndValue = refreshedSize > 0 ? refreshedSize - 1 : 0;
  startVar_.SetInt(0);
  endVar_.SetInt(newEndValue);
  updateSampleParameters();
  addAllFields();
  fullWaveformRedraw_ = true;
  playbackPosition_ = 0.0f;

  Trace::Log("SAMPLEEDITOR",
             "Trimmed sample '%s' to %u frames (start=%u, end=%u)",
             workingFilename.c_str(), trimResult.framesKept,
             trimResult.clampedStart, trimResult.clampedEnd);
  return true;
}

bool SampleEditorView::applyNormalizeOperation() {
  if (Player::GetInstance()->IsPlaying()) {
    Player::GetInstance()->StopStreaming();
  }
  isPlaying_ = false;
  playKeyHeld_ = false;

  if (!viewData_) {
    Trace::Error("SampleEditorView: View data unavailable");
    return false;
  }

  const auto &workingFilename = activeFilename();
  if (workingFilename.empty()) {
    Trace::Error("SampleEditorView: No filename available for normalize");
    return false;
  }
  const auto &displayFilename = viewData_->sampleEditorFilename;

  if (tempSampleSize_ == 0) {
    Trace::Error("SampleEditorView: Cannot normalize empty sample");
    return false;
  }

  SampleEditProgressDisplay progressDisplay(displayFilename);
  WavNormalizeResult normalizeResult{};
  sampleEditProgressDisplay = &progressDisplay;
  bool normalized = WavFileWriter::NormalizeFile(
      workingFilename.c_str(), static_cast<void *>(chunkBuffer_),
      sizeof(chunkBuffer_), normalizeResult, wavProgressCallback);
  sampleEditProgressDisplay = nullptr;
  progressDisplay.Finish(normalized);
  if (!normalized) {
    return false;
  }

  if (!normalizeResult.normalized) {
    updateSampleParameters();
    fullWaveformRedraw_ = true;
    Trace::Log("SAMPLEEDITOR",
               "Normalize skipped for '%s' (peak=%d, target=%d)",
               workingFilename.c_str(), normalizeResult.peakBefore,
               normalizeResult.targetPeak);
    return true;
  }

  loadSample(activeFilename());
  if (tempSampleSize_ == 0) {
    Trace::Error("SampleEditorView: Failed to reload normalized sample");
    return false;
  }

  uint32_t maxIndex = tempSampleSize_ - 1;
  uint32_t startVal = startVar_.GetInt();
  uint32_t endVal = endVar_.GetInt();
  if (startVal >= tempSampleSize_) {
    startVal = maxIndex;
  }
  if (endVal >= tempSampleSize_) {
    endVal = maxIndex;
  }
  if (startVal > endVal) {
    startVal = endVal;
  }
  startVar_.SetInt(startVal);
  endVar_.SetInt(endVal);

  updateSampleParameters();
  addAllFields();
  fullWaveformRedraw_ = true;
  playbackPosition_ = 0.0f;

  Trace::Log("SAMPLEEDITOR",
             "Normalized sample '%s' (gain=%.3f peak=%d target=%d)",
             workingFilename.c_str(), normalizeResult.gainApplied,
             normalizeResult.peakBefore, normalizeResult.targetPeak);
  return true;
}

bool SampleEditorView::reloadEditedSample() {
  loadSample(viewData_->sampleEditorFilename);

  auto pool = SamplePool::GetInstance();

  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> leafName;
  if (!extractLeafName(viewData_->sampleEditorFilename.c_str(), leafName)) {
    Trace::Error("SampleEditorView: Failed to extract pool filename from %s",
                 viewData_->sampleEditorFilename.c_str());
    return false;
  }

  int32_t old_index = pool->FindSampleIndexByName(leafName);
  if (old_index < 0) {
    Trace::Error("SampleEditorView: Sample %s not found in pool for reload",
                 leafName.c_str());
    return false;
  }

  // ReloadSample unloads the current pool entry first. The resulting delete
  // notification clears instruments assigned to that entry, so remember them
  // before the reload takes place.
  auto instrumentBank = viewData_->project_->GetInstrumentBank();
  auto assignedInstruments =
      instrumentBank->CaptureSampleAssignments(old_index);

  int32_t new_index =
      pool->ReloadSample(old_index, viewData_->sampleEditorFilename.c_str());
  if (new_index < 0) {
    Trace::Error("SampleEditorView: Failed to refresh pool sample %s",
                 leafName.c_str());
    return false;
  }

  instrumentBank->RestoreSampleAssignments(assignedInstruments, new_index);
  return true;
}

bool SampleEditorView::resolveSaveFilename(
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &filename) {
  filename =
      etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(filenameVar_.GetString());
  if (filename.empty()) {
    Trace::Error("SampleEditorView: Cannot save sample with empty name");
    return false;
  }

  filename.append(".wav");
  if (filename.is_truncated()) {
    Trace::Error("SampleEditorView: Filename too long");
    return false;
  }

  return true;
}

bool SampleEditorView::resolveSavePath(
    const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &filename,
    etl::string<PFILENAME_SIZE> &resolvedPath) const {
  resolvedPath.clear();
  if (filename.empty()) {
    return false;
  }

  etl::string<PFILENAME_SIZE> parentPath;
  if (!extractParentPath(viewData_->sampleEditorFilename.c_str(), parentPath)) {
    return false;
  }

  FileSystemPath::PathString joinedPath;
  if (!FileSystemPath::JoinPath(parentPath.c_str(), filename.c_str(),
                                joinedPath)) {
    return false;
  }

  resolvedPath = joinedPath.c_str();
  return !resolvedPath.is_truncated();
}

bool SampleEditorView::fileExists(
    const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &filename) {
  auto fs = FileSystem::GetInstance();
  etl::string<PFILENAME_SIZE> resolvedPath;
  if (!resolveSavePath(filename, resolvedPath)) {
    Trace::Error("SampleEditorView: Failed to resolve save path for %s",
                 filename.c_str());
    return true;
  }

  const auto &originalFilename = viewData_->sampleEditorFilename;
  if (resolvedPath == originalFilename) {
    return false;
  }

  return fs->exists(resolvedPath.c_str());
}

void SampleEditorView::attemptSave(bool loadToPool) {
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> filename;
  if (!resolveSaveFilename(filename)) {
    showSaveFailedDialog();
    return;
  }

  if (fileExists(filename)) {
    pendingOverwriteLoadToPool_ = loadToPool;
    MessageBox *confirmBox =
        MessageBox::Create(*this, "Overwrite existing sample?",
                           filename.c_str(), MBBF_OK | MBBF_CANCEL);
    DoModal(
        confirmBox,
        ModalViewCallback::create<SampleEditorView,
                                  &SampleEditorView::onConfirmOverwriteSave>(
            *this));
    return;
  }

  confirmSave(loadToPool);
}

void SampleEditorView::confirmSave(bool loadToPool) {
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> filename;
  if (!saveSample(filename)) {
    showSaveFailedDialog();
    Trace::Error("SampleEditorView: Failed to save file!");
    return;
  }

  if (loadToPool) {
    if (loadSampleToPool(filename)) {
      viewData_->isShowingSampleEditorProjectPool = true;
      navigateToView(VT_IMPORT);
    } else {
      showLoadToPoolFailedDialog();
    }
    return;
  }

  const auto &originalFilename = viewData_->sampleEditorFilename;
  if (pathIsWithinDirectory(originalFilename.c_str(), RECORDINGS_DIR)) {
    viewData_->importViewStartDir = RECORDINGS_DIR;
  }
  ViewType vt = SampleEditorView::sourceViewType_;
  navigateToView(vt);
}

void SampleEditorView::showSaveFailedDialog() {
  MessageBox *errorBox = MessageBox::Create(*this, "Save Failed",
                                            "Unable to save sample", MBBF_OK);
  DoModal(errorBox,
          ModalViewCallback::create<SampleEditorView,
                                    &SampleEditorView::onSimpleModalDismiss>(
              *this));
}

void SampleEditorView::showLoadToPoolFailedDialog() {
  MessageBox *errorBox =
      MessageBox::Create(*this, "Sample saved", "Pool load failed", MBBF_OK);
  DoModal(errorBox,
          ModalViewCallback::create<SampleEditorView,
                                    &SampleEditorView::onSimpleModalDismiss>(
              *this));
}

bool SampleEditorView::saveSample(
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &savedFilename) {
  auto fs = FileSystem::GetInstance();
  const auto &originalFilename = viewData_->sampleEditorFilename;

  if (!resolveSaveFilename(savedFilename)) {
    return false;
  }

  etl::string<PFILENAME_SIZE> resolvedSavePath;
  if (!resolveSavePath(savedFilename, resolvedSavePath)) {
    Trace::Error("SampleEditorView: Failed to resolve save path for %s",
                 savedFilename.c_str());
    return false;
  }

  const bool commitToOriginal = (resolvedSavePath == originalFilename);
  const bool committingWorkingCopy = hasWorkingCopy_;

  if (committingWorkingCopy) {
    if (!commitWorkingCopy(resolvedSavePath)) {
      Trace::Error("SampleEditorView: Save failed committing temp to %s",
                   resolvedSavePath.c_str());
      return false;
    }
  } else if (originalFilename != resolvedSavePath) {
    if (fs->exists(resolvedSavePath.c_str()) &&
        !fs->DeleteFile(resolvedSavePath.c_str())) {
      Trace::Error("SampleEditorView: Save failed deleting existing %s",
                   resolvedSavePath.c_str());
      return false;
    }
    if (!fs->CopyFile(originalFilename.c_str(), resolvedSavePath.c_str())) {
      Trace::Error("SampleEditorView: Save failed copying %s -> %s",
                   originalFilename.c_str(), resolvedSavePath.c_str());
      return false;
    }
  }

  if (viewData_->isShowingSampleEditorProjectPool && committingWorkingCopy &&
      commitToOriginal && !reloadEditedSample()) {
    Trace::Error("SampleEditorView: Save committed but failed pool refresh");
  }

  Trace::Log("SampleEditor", "Saved %s->%s", originalFilename.c_str(),
             resolvedSavePath.c_str());

  return true;
}

bool SampleEditorView::loadSampleToPool(
    const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &savedFilename) {
  if (!viewData_ || !viewData_->project_) {
    Trace::Error("SampleEditorView: Project context unavailable");
    return false;
  }

  auto pool = SamplePool::GetInstance();
  if (!pool) {
    Trace::Error("SampleEditorView: SamplePool unavailable");
    return false;
  }

  int16_t sampleId = -1;

  if (!viewData_->isShowingSampleEditorProjectPool) {
    char projectName[MAX_PROJECT_NAME_LENGTH + 1];
    viewData_->project_->GetProjectName(projectName);

    etl::string<PFILENAME_SIZE> savedPath;
    if (!resolveSavePath(savedFilename, savedPath)) {
      Trace::Error(
          "SampleEditorView: Failed to resolve saved sample path for %s",
          savedFilename.c_str());
      return false;
    }

    sampleId = pool->ImportSample(savedPath.c_str(), projectName);
    if (sampleId < 0) {
      Trace::Error("SampleEditorView: Import failed for %s", savedPath.c_str());
      return false;
    }
  } else {
    sampleId = pool->FindSampleIndexByName(savedFilename);
    if (sampleId < 0) {
      Trace::Error("SampleEditorView: Sample %s not found in pool",
                   savedFilename.c_str());
      return false;
    }
  }
  return true;
}

void SampleEditorView::navigateToView(ViewType vt) {
  if (Player::GetInstance()->IsPlaying()) {
    Player::GetInstance()->StopStreaming();
  }
  // "clear" the prev screen by setting it to Song screen
  // now that we are leaving this screen
  SampleEditorView::sourceViewType_ = VT_SONG;

  isPlaying_ = false;
  playKeyHeld_ = false;
  discardWorkingCopy();

  ViewEvent ve(VET_SWITCH_VIEW, &vt);
  SetChanged();
  NotifyObservers(&ve);
}

void SampleEditorView::updateSampleParameters() {

  uint32_t sampleSize = 0;
  if (tempSampleSize_ > 0) {
    // --- use the temporary sample's size ---
    sampleSize = tempSampleSize_;
  }

  start_ = startVar_.GetInt();
  end_ = endVar_.GetInt();

  if (sampleSize == 0) {
    start_ = 0;
    end_ = 0;
    startVar_.SetInt(0);
    endVar_.SetInt(0);
    updateZoomWindow();
    return;
  }

  bool startAdjusted = false;
  bool endAdjusted = false;

  // Ensure parameters are within valid range to prevent end going before start
  if (start_ >= sampleSize) {
    start_ = sampleSize - 1;
    startAdjusted = true;
  }
  if (start_ < 0) {
    start_ = 0;
    startAdjusted = true;
  }
  if (end_ >= sampleSize) {
    end_ = sampleSize - 1;
    endAdjusted = true;
  }
  if (end_ < 0) {
    end_ = 0;
    endAdjusted = true;
  }
  if (start_ > end_) {
    end_ = start_;
    endAdjusted = true;
  }

  if (startAdjusted) {
    startVar_.SetInt(start_);
  }
  if (endAdjusted) {
    endVar_.SetInt(end_);
  }

  updateZoomWindow();
}

int16_t SampleEditorView::chunkBuffer_[512 * 2];

void SampleEditorView::loadSample(const etl::string<PFILENAME_SIZE> filename) {

  // Reset temporary sample state
  tempSampleSize_ = 0;
  headerInfo_ = WavHeaderInfo{};
  headerValid_ = false;
  graphField_.SetSampleSize(0);
  graphField_.InvalidateWaveform();

  if (filename.empty()) {
    Trace::Error("missing sample filename");
    return;
  }

  if (!FileSystemPath::IsAbsolutePath(filename.c_str())) {
    Trace::Error("SampleEditorView: Expected absolute sample path: %s",
                 filename.c_str());
    return;
  }

  auto file = FileSystem::GetInstance()->Open(filename.c_str(), "r");
  if (!file) {
    Trace::Error("SampleEditorView: Failed to open file: %s", filename.c_str());
    return;
  }
  Trace::Log("SAMPLEEDITOR", "Loaded for parsing: %s", filename.c_str());

  // --- 1. Read Header & Get Size ---
  auto headerResult = WavHeaderWriter::ReadHeader(file.get());
  if (!headerResult.has_value()) {
    Trace::Error("SampleEditorView: Failed to parse WAV header for %s (err=%d)",
                 filename.c_str(), static_cast<int>(headerResult.error()));
    return;
  }

  const WavHeaderInfo headerInfo = headerResult.value();
  headerInfo_ = headerInfo;
  headerValid_ = true;
  uint16_t numChannels = headerInfo.numChannels;
  uint16_t bitsPerSample = headerInfo.bitsPerSample;
  uint32_t dataChunkSize = headerInfo.dataChunkSize;

  // Added a check to ensure we don't try to use more channels than our buffer
  // supports
  if (numChannels > 2 || numChannels == 0 || bitsPerSample == 0 ||
      dataChunkSize == 0) {
    Trace::Error("SampleEditorView: Invalid or unsupported WAV header in %s",
                 filename.c_str());
    return;
  }
  // need to check as its possible for the user to copy an invalid file into the
  // projects pool ie. samples subdir
  if (bitsPerSample != 8 && bitsPerSample != 16) {
    Trace::Error(
        "SampleEditorView: Unsupported bit depth (%u) in WAV header for %s",
        bitsPerSample, filename.c_str());
    return;
  }

  uint32_t bytesPerFrame = numChannels * headerInfo.bytesPerSample;
  tempSampleSize_ = bytesPerFrame > 0 ? dataChunkSize / bytesPerFrame : 0;
  if (tempSampleSize_ == 0) {
    Trace::Error("SampleEditorView: Sample has zero frames in %s",
                 filename.c_str());
    return;
  }
  graphField_.SetSampleSize(tempSampleSize_);
  updateZoomWindow();

  // ensure we start streaming from the data chunk
  file->Seek(headerInfo.dataOffset, SEEK_SET);

  // --- 2. Prepare for Single-Pass Processing ---
  Trace::Log("SAMPLEEDITOR", "Parsing sample: %d frames, %d channels",
             tempSampleSize_, numChannels);

  // set start point variable to 0
  startVar_.SetInt(0);

  // set end point variable
  endVar_.SetInt(tempSampleSize_ > 0 ? tempSampleSize_ - 1 : 0);

  Trace::Log("SAMPLEEDITOR", "Loaded %d frames from %s", tempSampleSize_,
             filename.c_str());
  fullWaveformRedraw_ = true;
}
