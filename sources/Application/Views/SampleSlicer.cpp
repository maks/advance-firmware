/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "SampleSlicer.h"

#include "Application/AppWindow.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Model/Song.h"
#include "Application/Player/Player.h"
#include "FieldView.h"
#include "SampleWaveform.h"
#include "ViewData.h"
#include <algorithm>
#include <nanoprintf.h>

namespace {
constexpr uint8_t DefaultSliceNoteBase = 48;
constexpr size_t PlayheadMarker = SampleSlicer::MaxSlices;
constexpr size_t FocusPointMarker = SampleSlicer::MaxSlices + 1;
constexpr size_t PreviewStartMarker = SampleSlicer::MaxSlices + 2;
constexpr size_t PreviewLoopStartMarker = SampleSlicer::MaxSlices + 3;
constexpr size_t PreviewEndMarker = SampleSlicer::MaxSlices + 4;
} // namespace

SampleSlicer::SampleSlicer(const SampleSlicerConfig &config)
    : config_(config), sliceIndexVar_(FourCC::SampleInstrumentSlices, 0),
      sliceStartVar_(FourCC::SampleInstrumentStart, 0),
      autoSliceCountVar_(FourCC::Default, 4), instrumentIndex_(0),
      sampleSize_(0), graphPosition_(config.graphPosition),
      graphField_(graphPosition_, GraphWidth, GraphHeight), playKeyHeld_(false),
      previewActive_(false), previewNote_(DefaultSliceNoteBase),
      previewChannel_(0), previewPlayheadSample_(0),
      previewCursorVisible_(false), focusSampleHintVisible_(false),
      focusSampleHint_(0), previewFocusHintVisible_(false),
      previewRangeVisible_(false), previewRangeStart_(0),
      previewLoopStartVisible_(false), previewRangeLoopStart_(0),
      previewRangeEnd_(0) {
  sliceIndexLabel_[0] = '\0';
  zoomLabel_[0] = '\0';
  graphField_.SetShowBaseline(false);
}

SampleSlicer::~SampleSlicer() { stopPreview(nullptr); }

void SampleSlicer::AddObserver(I_Observer &observer) {
  sliceIndexVar_.AddObserver(observer);
  sliceStartVar_.AddObserver(observer);
}

bool SampleSlicer::SlicerInstrumentAccess::IsValid(
    bool allowSampleInstrument, bool allowSampleSourceInstrument) const {
  if (!instrument) {
    return false;
  }
  return (allowSampleInstrument && instrument->GetType() == IT_SAMPLE) ||
         (allowSampleSourceInstrument &&
          instrument->GetType() == IT_SAMPLE_SOURCE);
}

int32_t SampleSlicer::SlicerInstrumentAccess::GetSampleIndex() const {
  if (!instrument) {
    return -1;
  }
  if (instrument->GetType() == IT_SAMPLE) {
    return static_cast<SampleInstrument *>(instrument)->GetSampleIndex();
  }
  if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    return static_cast<SampleSourceInstrument *>(instrument)->GetSampleIndex();
  }
  return -1;
}

bool SampleSlicer::SlicerInstrumentAccess::IsSliceDefined(size_t index) const {
  if (!instrument) {
    return false;
  }
  if (instrument->GetType() == IT_SAMPLE) {
    return static_cast<SampleInstrument *>(instrument)->IsSliceDefined(index);
  }
  if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    return static_cast<SampleSourceInstrument *>(instrument)
        ->IsSliceDefined(index);
  }
  return false;
}

uint32_t
SampleSlicer::SlicerInstrumentAccess::GetSlicePoint(size_t index) const {
  if (!instrument) {
    return 0;
  }
  if (instrument->GetType() == IT_SAMPLE) {
    return static_cast<SampleInstrument *>(instrument)->GetSlicePoint(index);
  }
  if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    return static_cast<SampleSourceInstrument *>(instrument)
        ->GetSlicePoint(index);
  }
  return 0;
}

void SampleSlicer::SlicerInstrumentAccess::SetSlicePoint(size_t index,
                                                         uint32_t start) const {
  if (!instrument) {
    return;
  }
  if (instrument->GetType() == IT_SAMPLE) {
    static_cast<SampleInstrument *>(instrument)->SetSlicePoint(index, start);
  } else if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    static_cast<SampleSourceInstrument *>(instrument)
        ->SetSlicePoint(index, start);
  }
}

void SampleSlicer::SlicerInstrumentAccess::ClearSlices() const {
  if (!instrument) {
    return;
  }
  if (instrument->GetType() == IT_SAMPLE) {
    static_cast<SampleInstrument *>(instrument)->ClearSlices();
  } else if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    static_cast<SampleSourceInstrument *>(instrument)->ClearSlices();
  }
}

bool SampleSlicer::SlicerInstrumentAccess::HasSlicesForPlayback() const {
  if (!instrument) {
    return false;
  }
  if (instrument->GetType() == IT_SAMPLE) {
    return static_cast<SampleInstrument *>(instrument)->HasSlicesForPlayback();
  }
  if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    return static_cast<SampleSourceInstrument *>(instrument)
        ->HasSlicesForPlayback();
  }
  return false;
}

uint8_t SampleSlicer::SlicerInstrumentAccess::SliceNoteBase() const {
  if (instrument && instrument->GetType() == IT_SAMPLE) {
    return SampleInstrument::SliceNoteBase;
  }
  if (instrument && instrument->GetType() == IT_SAMPLE_SOURCE) {
    return SampleSourceInstrument::SliceNoteBase;
  }
  return DefaultSliceNoteBase;
}

Variable *SampleSlicer::SlicerInstrumentAccess::RootNoteVariable() const {
  if (!instrument) {
    return nullptr;
  }
  if (instrument->GetType() == IT_SAMPLE) {
    return static_cast<SampleInstrument *>(instrument)
        ->FindVariable(FourCC::SampleInstrumentRootNote);
  }
  if (instrument->GetType() == IT_SAMPLE_SOURCE) {
    return static_cast<SampleSourceInstrument *>(instrument)
        ->FindVariable(FourCC::SampleSourceInstrumentRootNote);
  }
  return nullptr;
}

void SampleSlicer::Reset() {
  stopPreview(nullptr);
  access_.instrument = nullptr;
  instrumentIndex_ = 0;
  sampleSize_ = 0;

  focusSampleHintVisible_ = false;
  focusSampleHint_ = 0;
  previewFocusHintVisible_ = false;
  previewRangeVisible_ = false;
  previewRangeStart_ = 0;
  previewLoopStartVisible_ = false;
  previewRangeLoopStart_ = 0;
  previewRangeEnd_ = 0;
  resetPreviewState();
  sliceIndexVar_.SetInt(0, false);
  sliceStartVar_.SetInt(0, false);
  autoSliceCountVar_.SetInt(4, false);
  graphField_.Reset();
  graphField_.SetShowBaseline(false);
  sliceIndexLabel_[0] = '\0';
  zoomLabel_[0] = '\0';
}

void SampleSlicer::Attach(I_Instrument *instrument, int32_t instrumentIndex) {
  stopPreview(nullptr);
  bool changed = access_.instrument != instrument;
  access_.instrument = instrument;
  instrumentIndex_ = instrumentIndex;
  if (!access_.IsValid(config_.allowSampleInstrument,
                       config_.allowSampleSourceInstrument)) {
    Reset();
    return;
  }
  if (changed) {
    graphField_.Reset();
    graphField_.SetShowBaseline(false);
    sliceIndexVar_.SetInt(0, false);
    previewFocusHintVisible_ = false;
    previewRangeVisible_ = false;
    previewRangeStart_ = 0;
    previewLoopStartVisible_ = false;
    previewRangeLoopStart_ = 0;
    previewRangeEnd_ = 0;
  }
  resetPreviewState();
  refreshSampleSize();
  updateSliceSelectionFromInstrument();
  UpdateZoomWindow();
  rebuildWaveform();
  graphField_.RequestFullRedraw();
  autoSliceCountVar_.SetInt(4, false);
}

void SampleSlicer::AddFields(etl::list<UIField *, 64> &fieldList,
                             I_Observer &observer) {
  for (auto &field : actionFields_) {
    field.RemoveObserver(observer);
  }
  fieldList.insert(fieldList.end(), &graphField_);
  intVarFields_.clear();
  staticFields_.clear();
  actionFields_.clear();

  GUIPoint position = config_.controlsPosition;
  UpdateLabels();
  staticFields_.emplace_back(position, sliceIndexLabel_);
  fieldList.insert(fieldList.end(), &staticFields_.back());

  position._x += 21;
  staticFields_.emplace_back(position, zoomLabel_);
  fieldList.insert(fieldList.end(), &staticFields_.back());

  position._x = config_.controlsPosition._x + 11;
  intVarFields_.emplace_back(position, autoSliceCountVar_, "%2d", 1,
                             static_cast<int32_t>(MaxSlices), 1, 4);
  fieldList.insert(fieldList.end(), &intVarFields_.back());

  position._x += 3;
  actionFields_.emplace_back("Slice", FourCC::ActionAutoSlice, position);
  fieldList.insert(fieldList.end(), &actionFields_.back());
  actionFields_.back().AddObserver(observer);
}

bool SampleSlicer::ProcessButtonMask(FieldView &view, ViewData &viewData,
                                     unsigned short mask, bool pressed) {
  if (!pressed) {
    if (playKeyHeld_ && !(mask & EPBM_PLAY)) {
      playKeyHeld_ = false;
      stopPreview(&viewData);
      graphField_.RequestFullRedraw();
      markDirty(view);
      return true;
    }
    return false;
  }

  if (!IsActive()) {
    return false;
  }

  const UIField *focus = view.GetFocus();
  bool graphFocused = HasGraphFocus(focus);
  // Embedded instrument fields can project a marker onto the graph; in that
  // state ALT+PLAY should preview the graph rather than fall through to
  // transport.
  bool previewFocus =
      graphFocused || ShouldPreviewFromProjectedFocus(focusSampleHintVisible_,
                                                      previewFocusHintVisible_);
  bool slicerControlFocused = HasControlFocus(focus);
  if ((mask & EPBM_PLAY) && (mask & EPBM_ALT) && !(mask & EPBM_NAV) &&
      !(mask & EPBM_EDIT) &&
      ShouldHandlePreview(previewFocus, config_.allowNavBack,
                          slicerControlFocused)) {
    if (!playKeyHeld_) {
      startPreview(viewData);
      playKeyHeld_ = true;
      graphField_.RequestFullRedraw();
      markDirty(view);
    }
    return true;
  }

  if (graphFocused && (mask == EPBM_LEFT)) {
    int32_t index = sliceIndexVar_.GetInt();
    if (index > 0) {
      sliceIndexVar_.SetInt(index - 1);
    }
    return true;
  }
  if (graphFocused && (mask == EPBM_RIGHT)) {
    int32_t index = sliceIndexVar_.GetInt();
    if (index < static_cast<int32_t>(MaxSlices) - 1) {
      sliceIndexVar_.SetInt(index + 1);
    }
    return true;
  }

  if (graphFocused && (mask & EPBM_ENTER)) {
    uint32_t viewStart = graphField_.ViewStart();
    uint32_t viewEnd = graphField_.ViewEnd();
    uint32_t viewSpan = (viewEnd > viewStart) ? (viewEnd - viewStart) : 0;
    if (viewSpan == 0) {
      UpdateZoomWindow();
      viewStart = graphField_.ViewStart();
      viewEnd = graphField_.ViewEnd();
      viewSpan = (viewEnd > viewStart) ? (viewEnd - viewStart) : 0;
    }
    if (viewSpan > 0) {
      int32_t delta = 0;
      if (mask & (EPBM_LEFT | EPBM_RIGHT)) {
        if (graphField_.ZoomLevel() >= graphField_.MaxZoomLevel()) {
          delta = 1;
        } else {
          delta = static_cast<int32_t>(std::max<uint32_t>(1, viewSpan / 64));
        }
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
        int32_t newStart = sliceStartVar_.GetInt() + delta;
        if (newStart < 0) {
          newStart = 0;
        }
        if (sampleSize_ > 0) {
          int32_t maxStart = static_cast<int32_t>(sampleSize_ - 1);
          if (newStart > maxStart) {
            newStart = maxStart;
          }
        }
        sliceStartVar_.SetInt(newStart);
        return true;
      }
    }
  }

  bool zoomAllowed = config_.allowNavBack || graphFocused;
  if (zoomAllowed && (mask & EPBM_EDIT) && (mask & EPBM_UP)) {
    adjustZoom(view, 1);
    return true;
  }
  if (zoomAllowed && (mask & EPBM_EDIT) && (mask & EPBM_DOWN)) {
    adjustZoom(view, -1);
    return true;
  }

  return false;
}

void SampleSlicer::Draw(View &view) {
  if (!IsActive()) {
    return;
  }
  if (!graphField_.WaveformValid()) {
    rebuildWaveform();
  }

  graphField_.SetMarkerCount(MaxSlices + 5);
  if (sampleSize_ > 0) {
    for (size_t i = 0; i < MaxSlices; ++i) {
      if (!access_.IsSliceDefined(i)) {
        graphField_.SetMarker(i, 0, CD_ACCENT, false);
        continue;
      }
      uint32_t start = access_.GetSlicePoint(i);
      if (i == 0 && start == 0 && !access_.HasSlicesForPlayback()) {
        graphField_.SetMarker(i, 0, CD_ACCENT, false);
        continue;
      }
      ColorDefinition color =
          (static_cast<int32_t>(i) == sliceIndexVar_.GetInt()) ? CD_HILITE2
                                                               : CD_ACCENT;
      graphField_.SetMarker(i, start, color, true);
    }
  } else {
    for (size_t i = 0; i < MaxSlices; ++i) {
      graphField_.SetMarker(i, 0, CD_ACCENT, false);
    }
  }

  if (previewCursorVisible_) {
    graphField_.SetMarker(PlayheadMarker, previewPlayheadSample_, CD_NORMAL,
                          true);
  } else {
    graphField_.SetMarker(PlayheadMarker, 0, CD_NORMAL, false);
  }

  bool showPreviewRange = ShouldUsePreviewRange(access_.HasSlicesForPlayback(),
                                                previewRangeVisible_) &&
                          sampleSize_ > 0;
  if (showPreviewRange) {
    uint32_t start = previewRangeStart_;
    uint32_t end = previewRangeEnd_;
    if (start >= sampleSize_) {
      start = sampleSize_ - 1;
    }
    if (end >= sampleSize_) {
      end = sampleSize_ - 1;
    }
    graphField_.SetMarker(PreviewStartMarker, start, CD_ACCENTALT, true);
    if (previewLoopStartVisible_) {
      uint32_t loopStart = previewRangeLoopStart_;
      if (loopStart >= sampleSize_) {
        loopStart = sampleSize_ - 1;
      }
      graphField_.SetMarker(PreviewLoopStartMarker, loopStart, CD_HILITE2, true,
                            GraphField::MarkerStyle::Dashed);
    } else {
      graphField_.SetMarker(PreviewLoopStartMarker, 0, CD_HILITE2, false,
                            GraphField::MarkerStyle::Dashed);
    }
    graphField_.SetMarker(PreviewEndMarker, end, CD_WARN, true);
  } else {
    graphField_.SetMarker(PreviewStartMarker, 0, CD_ACCENTALT, false);
    graphField_.SetMarker(PreviewLoopStartMarker, 0, CD_HILITE2, false,
                          GraphField::MarkerStyle::Dashed);
    graphField_.SetMarker(PreviewEndMarker, 0, CD_WARN, false);
  }

  if (focusSampleHintVisible_) {
    graphField_.SetMarker(FocusPointMarker, focusSampleHint_, CD_ACCENTALT,
                          true, GraphField::MarkerStyle::Dashed);
  } else {
    graphField_.SetMarker(FocusPointMarker, 0, CD_ACCENTALT, false,
                          GraphField::MarkerStyle::Dashed);
  }

  graphField_.DrawGraph(view);
}

void SampleSlicer::AnimationUpdate(View &view) {
  bool previousCursorVisible = previewCursorVisible_;
  uint32_t previousPlayheadSample = previewPlayheadSample_;
  previewCursorVisible_ = false;

  uint32_t playbackSample = 0;
  bool hasPlaybackPosition = false;
  if (access_.instrument &&
      ShouldTrackAutomaticPlayhead(access_.instrument->GetType())) {
    hasPlaybackPosition = Player::GetInstance()->GetInstrumentPlaybackPosition(
        access_.instrument, playbackSample);
  }
  if (!hasPlaybackPosition && previewActive_ && access_.instrument) {
    hasPlaybackPosition = access_.instrument->GetPlaybackPosition(
        static_cast<int>(previewChannel_), playbackSample);
  }
  if (hasPlaybackPosition) {
    if (sampleSize_ > 0 && playbackSample >= sampleSize_) {
      playbackSample = sampleSize_ - 1;
    }
    previewPlayheadSample_ = playbackSample;
    previewCursorVisible_ = true;
  }

  if (previewCursorVisible_ != previousCursorVisible ||
      previewPlayheadSample_ != previousPlayheadSample) {
    markDirty(view);
  }

  if (previousCursorVisible || previewCursorVisible_) {
    Draw(view);
  }
}

bool SampleSlicer::HandleVariableUpdate(View &view, uintptr_t fourcc) {
  switch (fourcc) {
  case FourCC::SampleInstrumentSlices:
    handleSliceSelectionChange(view, false);
    return true;
  case FourCC::SampleInstrumentStart:
    applySliceStart(view, static_cast<uint32_t>(sliceStartVar_.GetInt()));
    return true;
  case FourCC::ActionAutoSlice:
    return true;
  default:
    break;
  }
  return false;
}

bool SampleSlicer::HasGraphFocus(const UIField *focus) const {
  return focus == &graphField_;
}

UIField *SampleSlicer::GraphFieldControl() { return &graphField_; }

GraphField &SampleSlicer::Graph() { return graphField_; }

bool SampleSlicer::IsActive() const {
  return access_.IsValid(config_.allowSampleInstrument,
                         config_.allowSampleSourceInstrument);
}

bool SampleSlicer::HasSlicesForPlayback() const {
  return access_.HasSlicesForPlayback();
}

uint8_t SampleSlicer::PreviewNote() {
  int32_t rootNote = DefaultSliceNoteBase;
  if (Variable *rootNoteVar = access_.RootNoteVariable()) {
    rootNote = rootNoteVar->GetInt();
  }
  return PreviewNoteForState(access_.HasSlicesForPlayback(),
                             access_.SliceNoteBase(), sliceIndexVar_.GetInt(),
                             rootNote);
}

void SampleSlicer::AutoSliceEvenly(View &view) {
  if (!IsActive() || !refreshSampleSize()) {
    return;
  }

  int32_t count = autoSliceCountVar_.GetInt();
  if (count < 1) {
    return;
  }
  if (count > static_cast<int32_t>(MaxSlices)) {
    count = static_cast<int32_t>(MaxSlices);
  }

  access_.ClearSlices();
  if (count > 1) {
    for (int32_t i = 0; i < count; ++i) {
      uint32_t start =
          (static_cast<uint64_t>(sampleSize_) * static_cast<uint64_t>(i)) /
          static_cast<uint32_t>(count);
      access_.SetSlicePoint(static_cast<size_t>(i), start);
    }
  }

  updateSliceSelectionFromInstrument();
  if (UpdateZoomWindow()) {
    graphField_.InvalidateWaveform();
  }
  graphField_.RequestFullRedraw();
  UpdateLabels();
  markDirty(view);
}

// GraphField writes region-local primitives. Overlay backing preserves covered
// updates, so modal dismissal no longer schedules recovery frames or clears.

void SampleSlicer::SetFocusSampleHint(bool visible, uint32_t sample) {
  focusSampleHintVisible_ = visible;
  focusSampleHint_ = sample;
  if (sampleSize_ > 0 && focusSampleHint_ >= sampleSize_) {
    focusSampleHint_ = sampleSize_ - 1;
  }
}

void SampleSlicer::SetPreviewFocusHint(bool visible) {
  previewFocusHintVisible_ = visible;
}

void SampleSlicer::SetPreviewRangeHint(bool visible, uint32_t start,
                                       uint32_t loopStart, uint32_t end,
                                       bool loopStartVisible) {
  previewRangeVisible_ = visible;
  previewRangeStart_ = start;
  previewLoopStartVisible_ = loopStartVisible;
  previewRangeLoopStart_ = loopStart;
  previewRangeEnd_ = end;
  if (sampleSize_ == 0) {
    previewRangeStart_ = 0;
    previewLoopStartVisible_ = false;
    previewRangeLoopStart_ = 0;
    previewRangeEnd_ = 0;
    return;
  }
  if (previewRangeStart_ >= sampleSize_) {
    previewRangeStart_ = sampleSize_ - 1;
  }
  if (previewRangeLoopStart_ >= sampleSize_) {
    previewRangeLoopStart_ = sampleSize_ - 1;
  }
  if (previewRangeEnd_ > sampleSize_) {
    previewRangeEnd_ = sampleSize_;
  }
}

bool SampleSlicer::UpdateZoomWindow() {
  uint32_t centerSample =
      focusSampleHintVisible_ ? focusSampleHint_ : selectedSliceStart();
  bool changed = graphField_.UpdateZoomWindow(centerSample);
  if (changed) {
    UpdateLabels();
  }
  return changed;
}

void SampleSlicer::InvalidateWaveform() { graphField_.InvalidateWaveform(); }

uint32_t SampleSlicer::SampleSize() const { return sampleSize_; }

void SampleSlicer::UpdateLabels() {
  int32_t sliceIndex = sliceIndexVar_.GetInt();
  if (sliceIndex < 0) {
    sliceIndex = 0;
  }
  npf_snprintf(sliceIndexLabel_, sizeof(sliceIndexLabel_), "Slice: %2d",
               sliceIndex);

  uint32_t zoom = 1u;
  uint8_t level = graphField_.ZoomLevel();
  if (level < 31) {
    zoom = 1u << level;
  }
  npf_snprintf(zoomLabel_, sizeof(zoomLabel_), "Zoom:%3ux", zoom);
}

void SampleSlicer::resetPreviewState() {
  playKeyHeld_ = false;
  previewActive_ = false;
  previewNote_ = DefaultSliceNoteBase;
  previewChannel_ = 0;
  previewPlayheadSample_ = 0;
  previewCursorVisible_ = false;
}

bool SampleSlicer::refreshSampleSize() {
  sampleSize_ = 0;
  if (!IsActive()) {
    graphField_.SetSampleSize(0);
    return false;
  }
  int32_t sampleIndex = access_.GetSampleIndex();
  if (sampleIndex < 0) {
    graphField_.SetSampleSize(0);
    return false;
  }
  SoundSource *source = SamplePool::GetInstance()->GetSource(sampleIndex);
  if (!source) {
    graphField_.SetSampleSize(0);
    return false;
  }
  int32_t size = source->GetSize(0);
  sampleSize_ = (size > 0) ? static_cast<uint32_t>(size) : 0;
  graphField_.SetSampleSize(sampleSize_);
  return sampleSize_ > 0;
}

void SampleSlicer::rebuildWaveform() {
  graphField_.InvalidateWaveform();
  graphField_.ClearWaveformCache();

  if (!refreshSampleSize() || sampleSize_ == 0 ||
      graphField_.ViewEnd() <= graphField_.ViewStart()) {
    return;
  }

  SoundSource *source =
      SamplePool::GetInstance()->GetSource(access_.GetSampleIndex());
  if (!source) {
    return;
  }

  int32_t channels = source->GetChannelCount(0);
  int16_t *samples = static_cast<int16_t *>(source->GetSampleBuffer(0));
  if (!samples || channels <= 0) {
    return;
  }

  uint32_t viewStart = graphField_.ViewStart();
  uint32_t viewEnd = graphField_.ViewEnd();
  SampleWaveform::Build(samples, static_cast<uint32_t>(channels), viewStart,
                        viewEnd, graphField_.WaveformCache(),
                        GraphField::CacheSize, GraphHeight);
  graphField_.SetWaveformValid(true);
}

void SampleSlicer::updateSliceSelectionFromInstrument() {
  if (!IsActive()) {
    sliceStartVar_.SetInt(0, false);
    return;
  }

  int32_t index = sliceIndexVar_.GetInt();
  if (index < 0) {
    index = 0;
  }
  if (index >= static_cast<int32_t>(MaxSlices)) {
    index = static_cast<int32_t>(MaxSlices) - 1;
  }

  uint32_t start = access_.GetSlicePoint(static_cast<size_t>(index));
  bool hasSlicesForPlayback = access_.HasSlicesForPlayback();
  if (start == 0 && index > 0) {
    for (int32_t i = index - 1; i >= 0; --i) {
      uint32_t prev = access_.GetSlicePoint(static_cast<size_t>(i));
      if (prev > 0) {
        start = prev;
        break;
      }
      if (i == 0 && hasSlicesForPlayback) {
        start = 0;
        break;
      }
    }
  }
  sliceStartVar_.SetInt(static_cast<int32_t>(start), false);
}

void SampleSlicer::applySliceStart(View &view, uint32_t start) {
  if (!IsActive()) {
    return;
  }
  size_t index = static_cast<size_t>(sliceIndexVar_.GetInt());
  access_.SetSlicePoint(index, start);
  uint32_t stored = access_.GetSlicePoint(index);
  if (stored != start) {
    sliceStartVar_.SetInt(static_cast<int32_t>(stored), false);
  }
  if (UpdateZoomWindow()) {
    graphField_.InvalidateWaveform();
    UpdateLabels();
  }
  graphField_.RequestFullRedraw();
  markDirty(view);
}

void SampleSlicer::adjustZoom(View &view, int32_t delta) {
  if (sampleSize_ == 0) {
    return;
  }
  uint32_t centerSample =
      focusSampleHintVisible_ ? focusSampleHint_ : selectedSliceStart();
  if (graphField_.AdjustZoom(delta, centerSample)) {
    graphField_.InvalidateWaveform();
    graphField_.RequestFullRedraw();
    UpdateLabels();
    markDirty(view);
  }
}

void SampleSlicer::startPreview(ViewData &viewData) {
  if (!hasInstrumentSample()) {
    return;
  }

  stopPreview(&viewData);

  bool usePreviewRange = ShouldUsePreviewRange(access_.HasSlicesForPlayback(),
                                               previewRangeVisible_);
  uint32_t startSample =
      usePreviewRange ? previewRangeStart_ : selectedSliceStart();

  uint8_t note = PreviewNote();

  previewChannel_ = static_cast<unsigned short>(SONG_CHANNEL_COUNT - 1);
  Player::GetInstance()->PlayNote(static_cast<unsigned short>(instrumentIndex_),
                                  previewChannel_, note, 0x7F);
  previewNote_ = note;
  previewActive_ = true;
  previewPlayheadSample_ = startSample;
  previewCursorVisible_ = sampleSize_ > 0;
}

void SampleSlicer::stopPreview(ViewData *viewData) {
  if (!previewActive_) {
    return;
  }
  unsigned short channel = previewChannel_;
  if (viewData && viewData->song_) {
    channel = static_cast<unsigned short>(SONG_CHANNEL_COUNT - 1);
  }
  Player::GetInstance()->StopNote(static_cast<unsigned short>(instrumentIndex_),
                                  channel);
  previewActive_ = false;
  previewCursorVisible_ = false;
  graphField_.RequestFullRedraw();
}

void SampleSlicer::handleSliceSelectionChange(View &view, bool graphFocused) {
  updateSliceSelectionFromInstrument();
  if (UpdateZoomWindow()) {
    graphField_.InvalidateWaveform();
    graphField_.RequestFullRedraw();
  }
  UpdateLabels();
  markDirty(view);
  (void)graphFocused;
}

bool SampleSlicer::HasControlFocus(const UIField *focus) const {
  if (!focus) {
    return false;
  }
  for (const auto &field : intVarFields_) {
    if (&field == focus) {
      return true;
    }
  }
  for (const auto &field : actionFields_) {
    if (&field == focus) {
      return true;
    }
  }
  return false;
}

uint32_t SampleSlicer::selectedSliceStart() {
  int32_t start = sliceStartVar_.GetInt();
  if (start < 0) {
    return 0;
  }
  return static_cast<uint32_t>(start);
}

uint32_t SampleSlicer::sliceEndForIndex(size_t index, uint32_t start) const {
  if (!IsActive() || sampleSize_ == 0) {
    return 0;
  }
  uint32_t end = sampleSize_ - 1;
  for (size_t i = index + 1; i < MaxSlices; ++i) {
    if (!access_.IsSliceDefined(i)) {
      continue;
    }
    uint32_t nextStart = access_.GetSlicePoint(i);
    if (nextStart > start) {
      end = nextStart;
      break;
    }
  }
  return end;
}

bool SampleSlicer::hasInstrumentSample() const {
  return IsActive() && access_.GetSampleIndex() >= 0 && sampleSize_ > 0;
}

void SampleSlicer::markDirty(View &view) const { view.SetDirty(true); }
