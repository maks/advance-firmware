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

#ifndef _SAMPLE_SLICER_H_
#define _SAMPLE_SLICER_H_

#include "Application/Instruments/I_Instrument.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Externals/etl/include/etl/list.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "GraphField.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include <cstddef>
#include <cstdint>

class FieldView;
class View;
class ViewData;

struct SampleSlicerConfig {
  GUIPoint graphPosition;
  GUIPoint controlsPosition;
  bool allowNavBack = false;
  bool showStandaloneTitle = false;
  bool allowSampleInstrument = true;
  bool allowSampleSourceInstrument = true;
};

class SampleSlicer {
public:
  static constexpr size_t MaxSlices = 64;
  static constexpr int32_t GraphXOffset = GraphField::DefaultXOffset;
  static constexpr int32_t GraphWidth = GraphField::BitmapWidth;
  static constexpr int32_t GraphHeight = GraphField::BitmapHeight;

  explicit SampleSlicer(const SampleSlicerConfig &config);
  ~SampleSlicer();

  void AddObserver(I_Observer &observer);
  void Reset();
  void Attach(I_Instrument *instrument, int32_t instrumentIndex);
  void AddFields(etl::list<UIField *, 64> &fieldList, I_Observer &observer);
  bool ProcessButtonMask(FieldView &view, ViewData &viewData,
                         unsigned short mask, bool pressed);
  void Draw(View &view);
  void AnimationUpdate(View &view);
  bool HandleVariableUpdate(View &view, uintptr_t fourcc);

  bool HasGraphFocus(const UIField *focus) const;
  UIField *GraphFieldControl();
  GraphField &Graph();
  bool IsActive() const;
  bool HasSlicesForPlayback() const;
  static constexpr bool ShouldHandlePreview(bool graphFocused,
                                            bool previewFromAnyFocus,
                                            bool slicerControlFocused) {
    return graphFocused || previewFromAnyFocus || slicerControlFocused;
  }
  static constexpr bool
  ShouldPreviewFromProjectedFocus(bool samplePointFocused,
                                  bool previewFocusHintVisible) {
    return samplePointFocused || previewFocusHintVisible;
  }
  static constexpr bool ShouldUsePreviewRange(bool hasSlicesForPlayback,
                                              bool previewRangeVisible) {
    return previewRangeVisible && !hasSlicesForPlayback;
  }
  static constexpr bool ShouldShowPreviewLoopStart(bool usePreviewRange,
                                                   bool loopModeUsesLoopStart) {
    return usePreviewRange && loopModeUsesLoopStart;
  }
  static constexpr bool ShouldTrackAutomaticPlayhead(InstrumentType type) {
    return type == IT_SAMPLE_SOURCE;
  }
  static constexpr uint8_t PreviewNoteForState(bool hasSlicesForPlayback,
                                               uint8_t sliceNoteBase,
                                               int32_t sliceIndex,
                                               int32_t rootNote) {
    if (hasSlicesForPlayback) {
      if (sliceIndex < 0) {
        sliceIndex = 0;
      }
      if (sliceIndex >= static_cast<int32_t>(MaxSlices)) {
        sliceIndex = static_cast<int32_t>(MaxSlices) - 1;
      }
      return static_cast<uint8_t>(sliceNoteBase + sliceIndex);
    }
    if (rootNote < 0) {
      rootNote = 0;
    }
    if (rootNote > 127) {
      rootNote = 127;
    }
    return static_cast<uint8_t>(rootNote);
  }
  void AutoSliceEvenly(View &view);
  uint8_t PreviewNote();
  void SetFocusSampleHint(bool visible, uint32_t sample);
  void SetPreviewFocusHint(bool visible);
  void SetPreviewRangeHint(bool visible, uint32_t start, uint32_t loopStart,
                           uint32_t end, bool loopStartVisible);
  bool UpdateZoomWindow();
  void InvalidateWaveform();
  uint32_t SampleSize() const;
  void UpdateLabels();

private:
  struct SlicerInstrumentAccess {
    I_Instrument *instrument = nullptr;

    bool IsValid(bool allowSampleInstrument,
                 bool allowSampleSourceInstrument) const;
    int32_t GetSampleIndex() const;
    bool IsSliceDefined(size_t index) const;
    uint32_t GetSlicePoint(size_t index) const;
    void SetSlicePoint(size_t index, uint32_t start) const;
    void ClearSlices() const;
    bool HasSlicesForPlayback() const;
    uint8_t SliceNoteBase() const;
    Variable *RootNoteVariable() const;
  };

  void resetPreviewState();
  bool refreshSampleSize();
  void rebuildWaveform();
  void updateSliceSelectionFromInstrument();
  void applySliceStart(View &view, uint32_t start);
  void adjustZoom(View &view, int32_t delta);
  void startPreview(ViewData &viewData);
  void stopPreview(ViewData *viewData);
  void handleSliceSelectionChange(View &view, bool graphFocused);
  bool HasControlFocus(const UIField *focus) const;
  uint32_t selectedSliceStart();
  uint32_t sliceEndForIndex(size_t index, uint32_t start) const;
  bool hasInstrumentSample() const;
  void markDirty(View &view) const;

  SampleSlicerConfig config_;
  WatchedVariable sliceIndexVar_;
  WatchedVariable sliceStartVar_;
  Variable autoSliceCountVar_;

  etl::vector<UIIntVarField, 1> intVarFields_;
  etl::vector<UIStaticField, 2> staticFields_;
  etl::vector<UIActionField, 1> actionFields_;
  char sliceIndexLabel_[16];
  char zoomLabel_[16];

  SlicerInstrumentAccess access_;
  int32_t instrumentIndex_;
  uint32_t sampleSize_;
  GUIPoint graphPosition_;
  GraphField graphField_;
  bool playKeyHeld_;
  bool previewActive_;
  uint8_t previewNote_;
  unsigned short previewChannel_;
  uint32_t previewPlayheadSample_;
  bool previewCursorVisible_;
  bool focusSampleHintVisible_;
  uint32_t focusSampleHint_;
  bool previewFocusHintVisible_;
  bool previewRangeVisible_;
  uint32_t previewRangeStart_;
  bool previewLoopStartVisible_;
  uint32_t previewRangeLoopStart_;
  uint32_t previewRangeEnd_;
};

#endif
