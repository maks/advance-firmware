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

#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/InstrumentNameVariable.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIBitmaskVarField.h"
#include "BaseClasses/UIFM6AlgoField.h"
#include "BaseClasses/UIFM6ParamField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIPitchOffsetVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UITextField.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "FieldView.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "SampleIndexField.h"
#include "SampleSlicer.h"
#include "ViewData.h"
#include <cstddef>
#include <cstdint>

class InstrumentView : public FieldView, public I_Observer {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 692, 570, TEXT_FONT_NORMAL},
        {8, 600, 80, 120, TEXT_FONT_SMALL},
        {96, 600, 616, 120, TEXT_FONT_NORMAL},
        {700, 90, 20, 480, TEXT_FONT_TINY},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  GraphicsLayout GetGraphicsLayout() override;
  InstrumentView(GUIWindow &w, ViewData *data);
  virtual ~InstrumentView();
  void Reset();

  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
  virtual void OnFocus();
  virtual void AnimationUpdate() override;
  void onInstrumentTypeChange(bool updateUI = false);
  bool checkInstrumentModified();
  void resetInstrumentToDefaults();
  void applyProposedTypeChangeUI();

protected:
  void warpToNext(int offset);
  void onInstrumentChange();
  void fillStarLoomParameters();
  void fillFM6Parameters();
  void fillFM6OperatorPanel(FM6Instrument &instrument, int x, int y);
  void fillFM6GlobalPanel(FM6Instrument &instrument, int x, int y);
  void updateFM6FixedFrequencyLabel();
  void addFM6Label(int x, int y, const char *text);
  void addFM6SectionHeader(int x, int y, const char *text);
  void addFM6Param(int x, int y, FM6Instrument &instrument,
                   const FM6ParamDef &def);
  void fillSampleParameters();
  void fillSampleSourceParameters();
  void fillMidiParameters();
  void fillSIDParameters();
  void fillPicoSwarmParameters();
  void fillMSamplerParameters();
  void fillNoneParameters();
  I_Instrument *getInstrument();
  void Update(Observable &o, I_ObservableData *d);
  void refreshInstrumentFields();
  void addNameTextField(I_Instrument *instr, GUIPoint &position);
  void handleInstrumentExport();
  void invalidateInstrumentText();
  void setupEmbeddedSlicer();
  void addEmbeddedSlicerFields(GUIPoint &position);
  void drawEmbeddedSlicerWaveform();
  bool focusedEmbeddedSlicerPointSample(uint32_t &sample);
  bool focusedEmbeddedSlicerPreviewField();
  void updateEmbeddedSlicerPreviewRange();
  bool isEmbeddedSlicerActive() const;
  void updateSamplePositionFieldLimits(I_Instrument *instrument,
                                       int32_t sampleIndex);
  uint8_t instrumentPreviewNote(I_Instrument *instrument);
  bool startInstrumentPreview(unsigned short mask);
  void auditionParameterChange(FourCC parameter);
  void stopInstrumentPreview();
  int maxSelectableInstrumentTypeIndex() const;
  void updateInstrumentTypeSelectionAvailability();

private:
  void onConfirmInstrumentTypeChange(View &view, ModalView &dialog);
  void onConfirmResetInstrument(View &view, ModalView &dialog);
  bool requestSampleChange(int32_t newSampleIndex);
  void onConfirmSampleChange(View &view, ModalView &dialog);
  void onConfirmExportOverwrite(View &view, ModalView &dialog);
  static void AutoSliceConfirmCallback(View &view, ModalView &dialog);
  void onSimpleModalDismiss(View &view, ModalView &dialog);

  static constexpr size_t SliceCountLabelSize = 20;
  static constexpr size_t FM6FixedFrequencyLabelSize = 8;
  Project *project_;
  FourCC lastFocusID_;
  bool lastFocusWasSliceGraph_;
  WatchedVariable instrumentType_;
  int lastSampleIndex_;
  int8_t pendingSampleSourceLoopModeDirection_;
  enum class PreviewMode : uint8_t { None, Manual, Parameter };
  PreviewMode previewMode_;
  unsigned short previewChannel_;
  unsigned short previewInstrument_;
  // Which FM6 patch block the algorithm field's cursor points at: 1-6 for a
  // DX7 operator, FM6SelectionGlobal for the global block. Held here so it
  // survives the field rebuild an operator/global switch triggers.
  uint8_t fm6Selection_;
  char fm6FixedFrequencyLabel_[FM6FixedFrequencyLabelSize] = {};
  etl::string<SliceCountLabelSize> sliceCountLabel_;

  // Variables for export confirmation dialog
  I_Instrument *pendingPurgeInstrument_ = nullptr;
  I_Instrument *pendingSampleChangeInstrument_ = nullptr;
  int pendingSampleChangeNewIndex_ = -1;
  I_Instrument *exportInstrument_ = nullptr;
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> exportName_;
  InstrumentType pendingInstrumentType_ = IT_NONE;

  SampleSlicer embeddedSlicer_;

  etl::vector<UIIntVarField, 1> typeIntVarField_;
  etl::vector<SampleIndexField, 1> sampleIndexField_;
  etl::vector<UIActionField, 2> persistentActionField_;
  etl::vector<UIIntVarField, 40> intVarField_;
  etl::vector<UIActionField, 1> fm6ActionField_;
  etl::vector<UIFM6AlgoField, 1> fm6AlgoField_;
  etl::vector<UIFM6ParamField, 24> fm6ParamField_;
  etl::vector<UINoteVarField, 1> noteVarField_;
  etl::vector<UIPitchOffsetVarField, 3> pitchOffsetVarField_;
  etl::vector<UIStaticField, 24> staticField_;
  etl::vector<UIBigHexVarField, 4> bigHexVarField_;
  etl::vector<UIIntVarOffField, 2> intVarOffField_;
  etl::vector<UIActionField, 1> sampleActionField_;
  etl::vector<UIBitmaskVarField, 3> bitmaskVarField_;
  etl::vector<UITextField<MAX_INSTRUMENT_NAME_LENGTH>, 1> nameTextField_;
  etl::vector<InstrumentNameVariable, 1> nameVariables_;
};
#endif
