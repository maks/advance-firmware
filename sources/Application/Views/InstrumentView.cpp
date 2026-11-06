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

#include "InstrumentView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/AdvanceFM6/FM6Dx7Patch.h"
#include "Application/Instruments/FM6Instrument.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Instruments/StarLoomInstrument.h"
#include "Application/Instruments/StarLoomInstrumentControls.h"
#include "Application/Instruments/StarLoomInstrumentDSP.h"
#include "Application/Instruments/StarLoomInstrumentShapeOptions.h"
#include "Application/Instruments/picoSwarmInstrument.h"
#include "Application/Model/Config.h"
#include "Application/Player/PlayButtonBehavior.h"
#include "Application/Views/ImportView.h"
#include "Application/Views/InstrumentImportView.h"
#include "Application/Views/SampleEditorView.h"
#include "Application/Views/VoiceScreenUtils.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Externals/etl/include/etl/to_string.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "ModalDialogs/MessageBox.h"
#include "ModalDialogs/TextInputModalView.h"
#include <Application/Utils/stringutils.h>
#include <cstdint>
#include <nanoprintf.h>

static const char *InstrumentTypeDisplayNames[IT_LAST] = {
    "NONE",     "SAMPLE",    "SID",  "FM6",
    "STARLOOM", "PICOSWARM", "MIDI", "ORIGINAL SAMPLE"};

static constexpr int kMaxStandardInstrumentTypeIndex = IT_LAST - 2;
static constexpr int kMaxLegacyInstrumentTypeIndex = IT_LAST - 1;
static constexpr InstrumentType kDisplayIndexToInstrumentType[IT_LAST] = {
    IT_NONE,     IT_SAMPLE_SOURCE, IT_SID,  IT_FM6,
    IT_STARLOOM, IT_PICO_SWARM,    IT_MIDI, IT_SAMPLE};
static constexpr int kInstrumentTypeToDisplayIndex[IT_LAST] = {0, 7, 1, 6,
                                                               2, 3, 4, 5};

constexpr int32_t EmbeddedSlicerGraphXOffset = SampleSlicer::GraphXOffset;
constexpr int32_t EmbeddedSlicerGraphYOffset = 6 * CHAR_HEIGHT;

void formatFM6FixedFrequency(uint32_t milliHertz, uint8_t coarse, char *buffer,
                             size_t bufferSize) {
  switch (coarse & 0x03U) {
  case 0:
    npf_snprintf(buffer, bufferSize, "%u.%03uHz",
                 static_cast<unsigned>(milliHertz / 1000U),
                 static_cast<unsigned>(milliHertz % 1000U));
    break;
  case 1: {
    const uint32_t centiHertz = (milliHertz + 5U) / 10U;
    npf_snprintf(buffer, bufferSize, "%u.%02uHz",
                 static_cast<unsigned>(centiHertz / 100U),
                 static_cast<unsigned>(centiHertz % 100U));
  } break;
  case 2: {
    const uint32_t deciHertz = (milliHertz + 50U) / 100U;
    npf_snprintf(buffer, bufferSize, "%u.%01uHz",
                 static_cast<unsigned>(deciHertz / 10U),
                 static_cast<unsigned>(deciHertz % 10U));
  } break;
  default:
    npf_snprintf(buffer, bufferSize, "%uHz",
                 static_cast<unsigned>((milliHertz + 500U) / 1000U));
    break;
  }
}

int displayIndexFromInstrumentType(InstrumentType type) {
  if (type < 0 || type >= IT_LAST) {
    return 0;
  }
  return kInstrumentTypeToDisplayIndex[type];
}

GraphicsLayout InstrumentView::GetGraphicsLayout() {
  static constexpr GraphicsRegion regions[] = {
      {EmbeddedSlicerGraphXOffset, EmbeddedSlicerGraphYOffset, 660, 180}};
  if (!isEmbeddedSlicerActive())
    return {nullptr, 0};
  return {regions, 1};
}

InstrumentView::InstrumentView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), lastFocusWasSliceGraph_(false),
      instrumentType_(FourCC::VarInstrumentType, InstrumentTypeDisplayNames,
                      IT_LAST, 0),
      lastSampleIndex_(-1), pendingSampleSourceLoopModeDirection_(0),
      previewMode_(PreviewMode::None), previewChannel_(0),
      previewInstrument_(0), fm6Selection_(1),
      embeddedSlicer_(
          {GUIPoint(EmbeddedSlicerGraphXOffset, EmbeddedSlicerGraphYOffset),
           GUIPoint(1, 11), false, false, false, true}) {
  project_ = data->project_;
  embeddedSlicer_.AddObserver(*this);

  GUIPoint position = GUIPoint(5, 0);
  typeIntVarField_.emplace_back(position, *&instrumentType_, "Type: %s", 0,
                                kMaxStandardInstrumentTypeIndex, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*typeIntVarField_.rbegin()));
  (*typeIntVarField_.rbegin()).AddObserver(*this);
  updateInstrumentTypeSelectionAvailability();
  lastFocusID_ = FourCC::VarInstrumentType;

  // Create the name field with the actual instrument variable
  I_Instrument *instr = getInstrument();
  if (instr) {
    // NONE dont have a name field
    if (instr->GetType() != IT_NONE) {
      position._y = 2;
      addNameTextField(instr, position);
    }
  }

  // add ui action fields for exporting and importing instrument settings
  position._y = 1;

  persistentActionField_.emplace_back("Import", FourCC::ActionImport, position);
  fieldList_.insert(fieldList_.end(), &(*persistentActionField_.rbegin()));
  (*persistentActionField_.rbegin()).AddObserver(*this);
  lastFocusID_ = FourCC::ActionImport;

  position._x += 8;
  persistentActionField_.emplace_back("Export", FourCC::ActionExport, position);
  fieldList_.insert(fieldList_.end(), &(*persistentActionField_.rbegin()));
  (*persistentActionField_.rbegin()).AddObserver(*this);
  lastFocusID_ = FourCC::ActionExport;

  sliceCountLabel_.clear();
}

InstrumentView::~InstrumentView() {}

void InstrumentView::Reset() {
  embeddedSlicer_.Reset();
  lastSampleIndex_ = -1;
  exportInstrument_ = nullptr;
  exportName_.clear();
  lastFocusID_ = FourCC::VarInstrumentType;
  lastFocusWasSliceGraph_ = false;
  pendingSampleSourceLoopModeDirection_ = 0;
  previewMode_ = PreviewMode::None;
  instrumentType_.SetInt(0, false);
  updateInstrumentTypeSelectionAvailability();
  sliceCountLabel_.clear();
}

int InstrumentView::maxSelectableInstrumentTypeIndex() const {
  return project_->GetInstrumentBank()
                 ->IsLegacySampleInstrumentSelectionEnabled()
             ? kMaxLegacyInstrumentTypeIndex
             : kMaxStandardInstrumentTypeIndex;
}

void InstrumentView::updateInstrumentTypeSelectionAvailability() {
  if (!typeIntVarField_.empty()) {
    typeIntVarField_.front().SetMaximum(maxSelectableInstrumentTypeIndex());
  }
}

static void updateSliceCountLabel(etl::string<20> &label,
                                  I_Instrument *instrument) {
  int32_t count = 0;
  if (instrument && instrument->GetType() == IT_SAMPLE) {
    SampleInstrument *sampleInstrument =
        static_cast<SampleInstrument *>(instrument);
    for (size_t i = 0; i < SampleInstrument::MaxSlices; ++i) {
      if (sampleInstrument->IsSliceDefined(i)) {
        count++;
      }
    }
  } else if (instrument && instrument->GetType() == IT_SAMPLE_SOURCE) {
    SampleSourceInstrument *sampleSourceInstrument =
        static_cast<SampleSourceInstrument *>(instrument);
    for (size_t i = 0; i < SampleSourceInstrument::MaxSlices; ++i) {
      if (sampleSourceInstrument->IsSliceDefined(i)) {
        count++;
      }
    }
  }
  if (count <= 1) {
    label = "Slices: off";
  } else {
    label = "Slices: ";
    etl::format_spec format;
    format.width(2).fill(' ');
    etl::to_string(count, label, format, true);
  }
}

static bool
resetSampleSourceOscillatorIfSampleTooLarge(I_Instrument *instrument) {
  if (!instrument || instrument->GetType() != IT_SAMPLE_SOURCE) {
    return false;
  }

  SampleSourceInstrument *sampleSourceInstrument =
      static_cast<SampleSourceInstrument *>(instrument);
  return sampleSourceInstrument->ResetOscillatorLoopModeIfSampleTooLarge();
}

static bool skipSampleSourceOscillatorIfSampleTooLarge(I_Instrument *instrument,
                                                       int direction) {
  if (!instrument || instrument->GetType() != IT_SAMPLE_SOURCE) {
    return false;
  }

  SampleSourceInstrument *sampleSourceInstrument =
      static_cast<SampleSourceInstrument *>(instrument);
  return sampleSourceInstrument->SkipOscillatorLoopModeIfSampleTooLarge(
      direction);
}

void InstrumentView::addNameTextField(I_Instrument *instr, GUIPoint &position) {
  nameVariables_.emplace_back(instr);
  Variable &nameVar = *nameVariables_.rbegin();

  auto label =
      etl::make_string_with_capacity<MAX_UITEXTFIELD_LABEL_LENGTH>("Name: ");

  // Use an empty default name - we don't want to populate with sample filename
  // The display name will still be shown on the phrase screen via
  // GetDisplayName()
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> defaultName;

  nameTextField_.emplace_back(nameVar, position, label, FourCC::InstrumentName,
                              defaultName);
  fieldList_.insert(fieldList_.end(), &(*nameTextField_.rbegin()));
}

I_Instrument *InstrumentView::getInstrument() {
  int id = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  return bank->GetInstrument(id);
};

void InstrumentView::onInstrumentTypeChange(bool updateUI) {
  auto nuType = kDisplayIndexToInstrumentType[instrumentType_.GetInt()];
  I_Instrument *old = getInstrument();

  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();

  auto id = viewData_->currentInstrumentID_;
  // release prev instrument back to available pool
  if (old != nullptr) {
    // first check if the instrument type actually changed, because could be
    // user is at end of instrument list and just keeps pressing key combo to
    // trigger next instrument event again and again
    if (old->GetType() == nuType) {
      if (updateUI) {
        refreshInstrumentFields();
      }
      return;
    }
    if (old->GetType() == IT_SAMPLE_SOURCE) {
      invalidateInstrumentText();
      embeddedSlicer_.Reset();
    }
    bank->releaseInstrument(viewData_->currentInstrumentID_);
  }

  // now assign new instrument type to the current instrument slot id
  unsigned short result = bank->GetNextAndAssignID(nuType, id);

  if (result == NO_MORE_INSTRUMENT) {
    Trace::Error("INSTRUMENTVIEW", "Failed to assign new instrument type: %d",
                 nuType);

    // Try to find the next available instrument type
    bool found = false;
    for (int i = displayIndexFromInstrumentType(nuType) + 1;
         i <= maxSelectableInstrumentTypeIndex(); i++) {
      InstrumentType nextType = kDisplayIndexToInstrumentType[i];
      result = bank->GetNextAndAssignID(nextType, id);
      if (result != NO_MORE_INSTRUMENT) {
        Trace::Log("INSTRUMENTVIEW", "Assigned next available type: %d",
                   nextType);
        instrumentType_.SetInt(i, false);
        found = true;
        break; // Exit loop on success
      }
    }

    if (!found) {
      Trace::Log("INSTRUMENTVIEW",
                 "No other instrument types available, setting to NONE");
      instrumentType_.SetInt(displayIndexFromInstrumentType(IT_NONE), false);
    }

    refreshInstrumentFields();
    isDirty_ = true;
    return;
  }

  // Get the new instrument after type change
  I_Instrument *newInstr = getInstrument();
  if (newInstr) {
    Trace::Log("INSTRUMENTVIEW", "New instrument type: %d",
               newInstr->GetType());
  }

  // Refresh the UI fields for the new instrument type
  refreshInstrumentFields();

  // Mark the view as dirty to ensure it gets redrawn
  isDirty_ = true;
}

void InstrumentView::applyProposedTypeChangeUI() {
  instrumentType_.SetInt(displayIndexFromInstrumentType(pendingInstrumentType_),
                         false);
  onInstrumentTypeChange();
}

void InstrumentView::onInstrumentChange() {

  ClearFocus();

  I_Instrument *old = getInstrument();
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();

  if (getInstrument() != old) {
    getInstrument()->RemoveObserver(*this);
  };

  // update type field to match current instrument
  ((WatchedVariable *)&instrumentType_)
      ->SetInt(displayIndexFromInstrumentType(getInstrument()->GetType()));

  refreshInstrumentFields();
};

void InstrumentView::refreshInstrumentFields() {
  const bool restoreSampleFieldFocus =
      !sampleIndexField_.empty() && GetFocus() == &sampleIndexField_.back();
  const bool restoreFM6PresetFieldFocus =
      !fm6ActionField_.empty() && GetFocus() == &fm6ActionField_.back();

  // Dynamic fields are destroyed and recreated below.  Drop the focus pointer
  // first so FieldView cannot dereference a field after its container is
  // cleared.
  ClearFocus();

  for (auto &f : sampleIndexField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : intVarField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : fm6ActionField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : fm6AlgoField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : fm6ParamField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : bigHexVarField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : pitchOffsetVarField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : intVarOffField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : bitmaskVarField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : sampleActionField_) {
    f.RemoveObserver(*this);
  }

  fieldList_.clear();
  intVarField_.clear();
  fm6ActionField_.clear();
  fm6AlgoField_.clear();
  fm6ParamField_.clear();
  noteVarField_.clear();
  pitchOffsetVarField_.clear();
  staticField_.clear();
  bigHexVarField_.clear();
  intVarOffField_.clear();
  sampleActionField_.clear();
  bitmaskVarField_.clear();
  nameTextField_.clear();
  nameVariables_.clear();
  sampleIndexField_.clear();
  lastSampleIndex_ = -1;
  InstrumentType currentType =
      getInstrument() ? getInstrument()->GetType() : IT_NONE;
  if (currentType != IT_SAMPLE_SOURCE) {
    invalidateInstrumentText();
    embeddedSlicer_.Reset();
  }

  // first put back the type field as its shown on *all* instrument types
  fieldList_.insert(fieldList_.end(), &(*typeIntVarField_.rbegin()));
  lastFocusID_ = FourCC::VarInstrumentType;

  // Re-add the action fields for export and import only if not IT_NONE
  if (currentType != IT_NONE) {
    for (auto &action : persistentActionField_) {
      fieldList_.insert(fieldList_.end(), &action);
      action.AddObserver(*this); // Make sure observers are re-added
    }
  } else {
    // add back only the import field for IT_NONE
    // bit of a hack !!since we just assume that import is the first action
    // field
    fieldList_.insert(fieldList_.end(), &(*persistentActionField_.begin()));
    (*persistentActionField_.rbegin()).AddObserver(*this);
  }

  // Create a new nameTextField_ if the instrument type supports it
  if (currentType != IT_NONE) {
    I_Instrument *instr = getInstrument();
    if (instr) {
      GUIPoint position = GetAnchor();
      addNameTextField(instr, position);
    }
  }

  InstrumentType it = getInstrument()->GetType();
  switch (it) {
  case IT_NONE:
    fillNoneParameters();
    break;
  case IT_MIDI:
    fillMidiParameters();
    break;
  case IT_SAMPLE:
    fillSampleParameters();
    break;
  case IT_SAMPLE_SOURCE:
    fillSampleSourceParameters();
    break;
  case IT_SID:
    fillSIDParameters();
    break;
  case IT_FM6:
    fillFM6Parameters();
    break;
  case IT_STARLOOM:
    fillStarLoomParameters();
    break;
  case IT_PICO_SWARM:
    fillPicoSwarmParameters();
    break;
  case IT_LAST:
    // NA
    break;
  };

  for (auto field : fieldList_) {
    if (embeddedSlicer_.HasGraphFocus(field)) {
      if (lastFocusWasSliceGraph_) {
        SetFocus(field);
        break;
      }
      continue;
    }
    if (!lastFocusWasSliceGraph_ && field->HasVariable() &&
        ((UIIntVarField *)field)->GetVariableID() == lastFocusID_) {
      SetFocus(field);
      break;
    }
  }

  if (restoreSampleFieldFocus && !sampleIndexField_.empty()) {
    SetFocus(&sampleIndexField_.back());
  } else if (restoreFM6PresetFieldFocus && !fm6ActionField_.empty()) {
    SetFocus(&fm6ActionField_.back());
  }

  // observer all var fields so we can mark the instrument as modified
  // to be able to show confirmation dialog when switching instrument type
  for (auto &f : sampleIndexField_) {
    f.AddObserver(*this);
  }
  for (auto &f : intVarField_) {
    f.AddObserver(*this);
  }
  for (auto &f : bigHexVarField_) {
    f.AddObserver(*this);
  }
  for (auto &f : pitchOffsetVarField_) {
    f.AddObserver(*this);
  }
  for (auto &f : intVarOffField_) {
    f.AddObserver(*this);
  }
  for (auto &f : bitmaskVarField_) {
    f.AddObserver(*this);
  }
  for (auto &f : fm6ParamField_) {
    f.AddObserver(*this);
  }

  getInstrument()->AddObserver(*this);
}

void InstrumentView::fillNoneParameters() {}

void InstrumentView::fillPicoSwarmParameters() {
  picoSwarmInstrument *instrument =
      static_cast<picoSwarmInstrument *>(getInstrument());
  GUIPoint position = GetAnchor();
  position._y += 2;

  Variable *variable =
      instrument->FindVariable(FourCC::picoSwarmInstrumentDetune);
  intVarField_.emplace_back(
      UIIntVarField(position, *variable, "Detune: %2.2X", 0, 0x7F, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &intVarField_.back());

  position._y += 1;
  variable = instrument->FindVariable(FourCC::picoSwarmInstrumentMix);
  intVarField_.emplace_back(
      UIIntVarField(position, *variable, "Mix:    %2.2X", 0, 0x7F, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &intVarField_.back());

  position._y += 2;
  variable =
      instrument->FindVariable(FourCC::picoSwarmInstrumentTableAutomation);
  intVarField_.emplace_back(
      UIIntVarField(position, *variable, "Automation: %s", 0, 1, 1, 1));
  fieldList_.insert(fieldList_.end(), &intVarField_.back());

  position._y += 1;
  variable = instrument->FindVariable(FourCC::picoSwarmInstrumentTable);
  intVarOffField_.emplace_back(
      UIIntVarOffField(position, *variable, "Table: %2.2X", 0, 0x7F, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &intVarOffField_.back());
}

void InstrumentView::fillSIDParameters() {
  SIDInstrument *instrument = static_cast<SIDInstrument *>(getInstrument());
  const GUIPoint anchor = GetAnchor();
  const int labelX = anchor._x;
  static constexpr int voiceX[SIDInstrument::VoiceCount] = {11, 18, 25};
  const FourCC waveformIDs[SIDInstrument::VoiceCount] = {
      FourCC::SIDInstrumentWaveform, FourCC::SIDInstrument2Waveform,
      FourCC::SIDInstrument3Waveform};
  const FourCC pulseWidthIDs[SIDInstrument::VoiceCount] = {
      FourCC::SIDInstrumentPulseWidth, FourCC::SIDInstrument2PulseWidth,
      FourCC::SIDInstrument3PulseWidth};
  const FourCC syncIDs[SIDInstrument::VoiceCount] = {
      FourCC::SIDInstrumentVSync, FourCC::SIDInstrument2VSync,
      FourCC::SIDInstrument3VSync};
  const FourCC ringIDs[SIDInstrument::VoiceCount] = {
      FourCC::SIDInstrumentRingModulator, FourCC::SIDInstrument2RingModulator,
      FourCC::SIDInstrument3RingModulator};
  const FourCC adsrIDs[SIDInstrument::VoiceCount] = {
      FourCC::SIDInstrumentADSR, FourCC::SIDInstrument2ADSR,
      FourCC::SIDInstrument3ADSR};
  const FourCC filterOnIDs[SIDInstrument::VoiceCount] = {
      FourCC::SIDInstrumentFilterOn, FourCC::SIDInstrument2FilterOn,
      FourCC::SIDInstrument3FilterOn};
  const FourCC pitchOffsetIDs[SIDInstrument::VoiceCount] = {
      FourCC::SIDInstrumentPitchOffset, FourCC::SIDInstrument2PitchOffset,
      FourCC::SIDInstrument3PitchOffset};

  const auto addStatic = [this](int x, int y, const char *label) {
    staticField_.emplace_back(GUIPoint(x, y), label);
    fieldList_.insert(fieldList_.end(), &staticField_.back());
  };
  const auto addSectionHeader = [this](int x, int y, const char *label) {
    staticField_.emplace_back(GUIPoint(x, y), label, CD_HILITE2);
    fieldList_.insert(fieldList_.end(), &staticField_.back());
  };
  const auto addVoiceRow = [this, instrument, labelX](
                               int y, const char *label, const FourCC *ids,
                               const char *format, int min, int max,
                               int fieldXOffset, int xOffset, int yOffset) {
    staticField_.emplace_back(GUIPoint(labelX, y), label);
    fieldList_.insert(fieldList_.end(), &staticField_.back());
    for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
      Variable *variable = instrument->FindVariable(ids[voice]);
      intVarField_.emplace_back(GUIPoint(voiceX[voice] + fieldXOffset, y),
                                *variable, format, min, max, xOffset, yOffset);
      fieldList_.insert(fieldList_.end(), &intVarField_.back());
    }
  };
  const auto addChipField =
      [this, instrument](int x, int y, FourCC id, const char *format, int min,
                         int max, int xOffset, int yOffset) {
        Variable *variable = instrument->FindVariable(id);
        intVarField_.emplace_back(GUIPoint(x, y), *variable, format, min, max,
                                  xOffset, yOffset);
        fieldList_.insert(fieldList_.end(), &intVarField_.back());
      };

  // Offset y to account for instrument type, name and export/import fields.
  const int chipModelY = anchor._y + 2;
  addChipField(labelX, chipModelY, FourCC::SIDInstrumentChipModel,
               "Model:       %s", 0, SID_CHIP_LAST - 1, 1, 1);
  addChipField(labelX, chipModelY + 1, FourCC::SIDInstrument1Volume,
               "Volume:      %1.1X", 0, 0xF, 1, 1);

  const int voiceHeaderY = chipModelY + 3;
  addSectionHeader(5, voiceHeaderY, "Voices");
  addVoiceRow(voiceHeaderY + 1, "Wave:", waveformIDs, "%s", 0, DWF_LAST - 1, 0,
              1, 1);
  addStatic(labelX, voiceHeaderY + 2, "Tune:");
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    Variable *variable = instrument->FindVariable(pitchOffsetIDs[voice]);
    pitchOffsetVarField_.emplace_back(GUIPoint(voiceX[voice], voiceHeaderY + 2),
                                      *variable);
    fieldList_.insert(fieldList_.end(), &pitchOffsetVarField_.back());
  }
  addVoiceRow(voiceHeaderY + 3, "Wdth:", pulseWidthIDs, "%3.3X", 0, 0xFFF, 0, 1,
              0x40);
  addVoiceRow(voiceHeaderY + 4, "Sync:", syncIDs, "%s", 0, 1, 0, 1, 1);
  addVoiceRow(voiceHeaderY + 5, "Ring:", ringIDs, "%s", 0, 1, 0, 1, 1);

  const int adsrY = voiceHeaderY + 6;
  addStatic(labelX, adsrY, "ADSR:");
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    Variable *variable = instrument->FindVariable(adsrIDs[voice]);
    bigHexVarField_.emplace_back(UIBigHexVarField(
        GUIPoint(voiceX[voice], adsrY), *variable, 4, "%4.4X", 0, 0xFFFF, 16,
        false, UIBigHexEditMode::IndependentDigits));
    fieldList_.insert(fieldList_.end(), &bigHexVarField_.back());
  }
  addVoiceRow(voiceHeaderY + 7, "Filt:", filterOnIDs, "%s", 0, 1, 0, 1, 1);

  const int chipHeaderY = voiceHeaderY + 8;
  addSectionHeader(5, chipHeaderY, "Filter");
  addChipField(labelX, chipHeaderY + 1, FourCC::SIDInstrument1FilterCut,
               "Cutoff:      %3.3X", 0, 0x7FF, 1, 0x20);
  addChipField(labelX, chipHeaderY + 2, FourCC::SIDInstrument1FilterResonance,
               "Resonance:   %1.1X", 0, 0xF, 1, 1);
  addChipField(labelX, chipHeaderY + 3, FourCC::SIDInstrument1FilterMode,
               "Mode:        %s", 0, DFM_LAST - 1, 1, 1);

  const int tableY = chipHeaderY + 5;
  Variable *table = instrument->FindVariable(FourCC::SIDInstrumentTable);
  intVarOffField_.emplace_back(GUIPoint(labelX, tableY), *table, "Table: %2.2X",
                               0x00, TABLE_COUNT - 1, 1, 0x10);
  intVarOffField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &intVarOffField_.back());

  Variable *automation =
      instrument->FindVariable(FourCC::SIDInstrumentTableAutomation);
  intVarField_.emplace_back(GUIPoint(labelX + 13, tableY), *automation,
                            "Auto: %s", 0, 1, 1, 1);
  intVarField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &intVarField_.back());
}

void InstrumentView::fillSampleParameters() {
  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(i);
  SampleInstrument *instrument = (SampleInstrument *)instr;
  lastSampleIndex_ = instrument->GetSampleIndex();

  GUIPoint position = GetAnchor();
  const int baseX = position._x;

  // offset y to account for instrument type and export/import fields
  position._y += 1;

  Variable *v = instrument->FindVariable(FourCC::SampleInstrumentSample);
  SamplePool *sp = SamplePool::GetInstance();
  sampleIndexField_.emplace_back(
      position, *v, sp->GetNameListSize() - 1,
      SampleIndexField::ChangeRequest::create<
          InstrumentView, &InstrumentView::requestSampleChange>(*this));
  fieldList_.insert(fieldList_.end(), &sampleIndexField_.back());

  position._y += 1;
  updateSliceCountLabel(sliceCountLabel_, instrument);
  staticField_.emplace_back(position, sliceCountLabel_.c_str());
  fieldList_.insert(fieldList_.end(), &staticField_.back());

  GUIPoint actionPos = position;
  actionPos._x = baseX + 12;
  sampleActionField_.emplace_back("Adjust", FourCC::ActionShowSampleSlices,
                                  actionPos);
  fieldList_.insert(fieldList_.end(), &sampleActionField_.back());
  sampleActionField_.back().AddObserver(*this);

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentVolume);
  intVarField_.emplace_back(position, *v, "Volume: %d [%2.2X]", 0, 255, 1, 10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentPan);
  intVarField_.emplace_back(position, *v, "Pan: %2.2X", 0, 0xFE, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentRootNote);
  noteVarField_.emplace_back(position, *v, "Root Note: %s", 0, 0x7F, 1, 0x0C);
  fieldList_.insert(fieldList_.end(), &(*noteVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentFineTune);
  intVarField_.emplace_back(position, *v, "Detune: %2.2X", 0, 255, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentCrushVolume);
  intVarField_.emplace_back(position, *v, "Drive: %2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentCrush);
  intVarField_.emplace_back(position, *v, "Crush: %d", 1, 0x10, 1, 4);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentDownsample);
  intVarField_.emplace_back(position, *v, "Downsample: %d", 0, 8, 1, 4);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 2;
  staticField_.emplace_back(position, "Flt Cut/Res:");
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._x += 13;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterCutOff);
  intVarField_.emplace_back(position, *v, "%2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._x += 3;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterResonance);
  intVarField_.emplace_back(position, *v, "%2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._x -= 16;

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterType);
  intVarField_.emplace_back(position, *v, "Type: %2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterMode);
  intVarField_.emplace_back(position, *v, "Mode: %s", 0, 2, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentInterpolation);
  intVarField_.emplace_back(position, *v, "Interpolation: %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentLoopMode);
  intVarField_.emplace_back(position, *v, "Loop Mode: %s", 0, SILM_LAST - 1, 1,
                            1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentStart);
  bigHexVarField_.emplace_back(position, *v, 7, "Start: %7.7X", 0,
                               instrument->GetSampleSize() - 1, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentLoopStart);
  bigHexVarField_.emplace_back(position, *v, 7, "Loop Start: %7.7X", 0,
                               instrument->GetSampleSize() - 1, 16);
  bigHexVarField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentEnd);
  bigHexVarField_.emplace_back(position, *v, 7, "Loop End: %7.7X", 0,
                               instrument->GetSampleSize() - 1, 16);
  bigHexVarField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentTable);
  intVarOffField_.emplace_back(position, *v, "Table: %2.2X", 0x00,
                               TABLE_COUNT - 1, 1, 0x10);
  intVarOffField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));

  v = instrument->FindVariable(FourCC::SampleInstrumentTableAutomation);
  position._x += 12;
  intVarField_.emplace_back(position, *v, "Auto: %s", 0, 1, 1, 1);
  intVarField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
};

void InstrumentView::fillSampleSourceParameters() {
  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instrument = bank->GetInstrument(i);
  SampleSourceInstrument *sampleSourceInstrument =
      static_cast<SampleSourceInstrument *>(instrument);
  lastSampleIndex_ = sampleSourceInstrument->GetSampleIndex();

  GUIPoint position = GetAnchor();

  position._y += 1;

  Variable *v = sampleSourceInstrument->FindVariable(
      FourCC::SampleSourceInstrumentSample);
  SamplePool *sp = SamplePool::GetInstance();
  sampleIndexField_.emplace_back(
      position, *v, sp->GetNameListSize() - 1,
      SampleIndexField::ChangeRequest::create<
          InstrumentView, &InstrumentView::requestSampleChange>(*this));
  fieldList_.insert(fieldList_.end(), &sampleIndexField_.back());

  setupEmbeddedSlicer();
  ((AppWindow &)w_).SetDirty();
  addEmbeddedSlicerFields(position);

  GUIPoint leftColumn(1, 13);
  GUIPoint rightColumn = leftColumn;
  rightColumn._x = 17;

  position = leftColumn;
  v = sampleSourceInstrument->FindVariable(FourCC::SampleSourceInstrumentLevel);
  intVarField_.emplace_back(position, *v, "Level: %d [%2.2X]", 0, 255, 1, 16);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = sampleSourceInstrument->FindVariable(
      FourCC::SampleSourceInstrumentRootNote);
  noteVarField_.emplace_back(position, *v, "Root Note: %s", 0, 0x7F, 1, 0x0C);
  fieldList_.insert(fieldList_.end(), &(*noteVarField_.rbegin()));

  position._y += 1;
  v = sampleSourceInstrument->FindVariable(FourCC::SampleSourceInstrumentTable);
  intVarOffField_.emplace_back(position, *v, "Table: %2.2X", 0x00,
                               TABLE_COUNT - 1, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));

  v = sampleSourceInstrument->FindVariable(
      FourCC::SampleSourceInstrumentTableAutomation);
  position._y += 1;
  intVarField_.emplace_back(position, *v, "Auto: %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position = rightColumn;
  v = sampleSourceInstrument->FindVariable(
      FourCC::SampleSourceInstrumentLoopMode);
  intVarField_.emplace_back(position, *v, "Mode: %.8s", 0, SSLM_LAST - 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = sampleSourceInstrument->FindVariable(FourCC::SampleSourceInstrumentStart);
  bigHexVarField_.emplace_back(position, *v, 7, "Start: %7.7X", 0,
                               sampleSourceInstrument->GetSampleSize() - 1, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = sampleSourceInstrument->FindVariable(
      FourCC::SampleSourceInstrumentLoopStart);
  bigHexVarField_.emplace_back(position, *v, 7, "Loop:  %7.7X", 0,
                               sampleSourceInstrument->GetSampleSize() - 1, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = sampleSourceInstrument->FindVariable(FourCC::SampleSourceInstrumentEnd);
  bigHexVarField_.emplace_back(position, *v, 7, "End:   %7.7X", 0,
                               sampleSourceInstrument->GetSampleSize() - 1, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));
}

void InstrumentView::invalidateInstrumentText() {
  // The slicer may still refer to an instrument that was released while this
  // view was unfocused (for example, Remove Unused Instruments).  Do not query
  // that pointer here.
  ((AppWindow &)w_).InvalidateTextCache();
  ((AppWindow &)w_).SetDirty();
}

void InstrumentView::setupEmbeddedSlicer() {
  I_Instrument *instrument = getInstrument();
  if (!instrument || instrument->GetType() != IT_SAMPLE_SOURCE) {
    embeddedSlicer_.Reset();
    return;
  }

  embeddedSlicer_.Attach(instrument,
                         static_cast<int32_t>(viewData_->currentInstrumentID_));
  updateEmbeddedSlicerPreviewRange();
}

void InstrumentView::addEmbeddedSlicerFields(GUIPoint &position) {
  embeddedSlicer_.AddFields(fieldList_, *this);
  position._x = GetAnchor()._x;
}

bool InstrumentView::isEmbeddedSlicerActive() const {
  return embeddedSlicer_.IsActive();
}

uint8_t InstrumentView::instrumentPreviewNote(I_Instrument *instrument) {
  if (!instrument || instrument->GetType() != IT_SAMPLE_SOURCE) {
    return NOTE_C3;
  }
  SampleSourceInstrument *sampleSourceInstrument =
      static_cast<SampleSourceInstrument *>(instrument);
  return embeddedSlicer_.IsActive() ? embeddedSlicer_.PreviewNote()
                                    : sampleSourceInstrument->GetPreviewNote();
}

bool InstrumentView::startInstrumentPreview(unsigned short mask) {
  if (!IsInstrumentPreviewButtonMask(mask, EPBM_PLAY, EPBM_ALT)) {
    return false;
  }

  I_Instrument *instrument = getInstrument();
  if (!SupportsInstrumentPreview(instrument) || !viewData_->song_) {
    return false;
  }

  if (previewMode_ != PreviewMode::None) {
    return true;
  }

  int channelCount = SONG_CHANNEL_COUNT;
  if (channelCount <= 0) {
    return true;
  }

  previewInstrument_ =
      static_cast<unsigned short>(viewData_->currentInstrumentID_);
  previewChannel_ = static_cast<unsigned short>(channelCount - 1);
  Player::GetInstance()->PlayNote(previewInstrument_, previewChannel_,
                                  instrumentPreviewNote(instrument), 0x7F);
  previewMode_ = PreviewMode::Manual;
  return true;
}

void InstrumentView::auditionParameterChange(FourCC parameter) {
  Config *config = Config::GetInstance();
  if (!config || !config->IsInstrumentAuditionEnabled()) {
    return;
  }

  I_Instrument *instrument = getInstrument();
  Player *player = Player::GetInstance();
  if (!instrument || !viewData_->song_ || !player || player->IsRunning() ||
      !instrument->ShouldAuditionParameterChange(parameter)) {
    return;
  }

  // Parameters such as oscillator shape are captured at note start, so every
  // accepted change gets a fresh note rather than reusing a sounding voice.
  if (previewMode_ != PreviewMode::None) {
    stopInstrumentPreview();
  }
  previewInstrument_ =
      static_cast<unsigned short>(viewData_->currentInstrumentID_);
  previewChannel_ = static_cast<unsigned short>(SONG_CHANNEL_COUNT - 1);
  player->PlayNote(previewInstrument_, previewChannel_,
                   instrumentPreviewNote(instrument), 0x7F);
  previewMode_ = PreviewMode::Parameter;
}

void InstrumentView::stopInstrumentPreview() {
  if (previewMode_ == PreviewMode::None) {
    return;
  }

  Player::GetInstance()->StopNote(previewInstrument_, previewChannel_);
  previewMode_ = PreviewMode::None;
}

void InstrumentView::drawEmbeddedSlicerWaveform() {
  if (!isEmbeddedSlicerActive()) {
    return;
  }
  uint32_t sample = 0;
  embeddedSlicer_.SetFocusSampleHint(focusedEmbeddedSlicerPointSample(sample),
                                     sample);
  embeddedSlicer_.SetPreviewFocusHint(focusedEmbeddedSlicerPreviewField());
  updateEmbeddedSlicerPreviewRange();
  embeddedSlicer_.Draw(*this);
}

void InstrumentView::AutoSliceConfirmCallback(View &view, ModalView &dialog) {
  auto &self = static_cast<InstrumentView &>(view);
  self.isDirty_ = true;
  ((AppWindow &)self.w_).SetDirty();
  if (dialog.GetReturnCode() != MBL_YES) {
    return;
  }
  self.embeddedSlicer_.AutoSliceEvenly(self);
  I_Instrument *instrument = self.getInstrument();
  updateSliceCountLabel(self.sliceCountLabel_, instrument);
}

bool InstrumentView::focusedEmbeddedSlicerPointSample(uint32_t &sample) {
  if (!isEmbeddedSlicerActive() || embeddedSlicer_.SampleSize() == 0) {
    return false;
  }

  UIField *focus = GetFocus();
  for (auto &field : bigHexVarField_) {
    if (&field != focus) {
      continue;
    }

    FourCC id = field.GetVariableID();
    if (id != FourCC::SampleSourceInstrumentStart &&
        id != FourCC::SampleSourceInstrumentLoopStart &&
        id != FourCC::SampleSourceInstrumentEnd) {
      return false;
    }

    int32_t value = field.GetVariable().GetInt();
    sample = value > 0 ? static_cast<uint32_t>(value) : 0;
    if (sample >= embeddedSlicer_.SampleSize()) {
      sample = embeddedSlicer_.SampleSize() - 1;
    }
    return true;
  }
  return false;
}

bool InstrumentView::focusedEmbeddedSlicerPreviewField() {
  if (!isEmbeddedSlicerActive()) {
    return false;
  }

  UIField *focus = GetFocus();
  for (auto &field : intVarField_) {
    if (&field == focus &&
        field.GetVariableID() == FourCC::SampleSourceInstrumentLoopMode) {
      return true;
    }
  }
  return false;
}

void InstrumentView::updateEmbeddedSlicerPreviewRange() {
  if (!isEmbeddedSlicerActive() || embeddedSlicer_.SampleSize() == 0) {
    embeddedSlicer_.SetPreviewRangeHint(false, 0, 0, 0, false);
    return;
  }

  I_Instrument *instrument = getInstrument();
  if (!instrument || instrument->GetType() != IT_SAMPLE_SOURCE ||
      embeddedSlicer_.HasSlicesForPlayback()) {
    embeddedSlicer_.SetPreviewRangeHint(false, 0, 0, 0, false);
    return;
  }

  SampleSourceInstrument *sampleSourceInstrument =
      static_cast<SampleSourceInstrument *>(instrument);
  Variable *startVar =
      sampleSourceInstrument->FindVariable(FourCC::SampleSourceInstrumentStart);
  Variable *loopModeVar = sampleSourceInstrument->FindVariable(
      FourCC::SampleSourceInstrumentLoopMode);
  Variable *loopStartVar = sampleSourceInstrument->FindVariable(
      FourCC::SampleSourceInstrumentLoopStart);
  Variable *endVar =
      sampleSourceInstrument->FindVariable(FourCC::SampleSourceInstrumentEnd);
  if (!startVar || !loopModeVar || !loopStartVar || !endVar) {
    embeddedSlicer_.SetPreviewRangeHint(false, 0, 0, 0, false);
    return;
  }

  int32_t startValue = startVar->GetInt();
  int32_t loopStartValue = loopStartVar->GetInt();
  int32_t endValue = endVar->GetInt();
  uint32_t start = startValue > 0 ? static_cast<uint32_t>(startValue) : 0;
  uint32_t loopStart =
      loopStartValue > 0 ? static_cast<uint32_t>(loopStartValue) : 0;
  uint32_t end = endValue > 0 ? static_cast<uint32_t>(endValue) : 0;
  bool loopStartVisible = SampleSlicer::ShouldShowPreviewLoopStart(
      true,
      SampleSourceInstrument::PlaybackModeUsesLoopPoint(loopModeVar->GetInt()));
  embeddedSlicer_.SetPreviewRangeHint(true, start, loopStart, end,
                                      loopStartVisible);
}

bool InstrumentView::requestSampleChange(int32_t newSampleIndex) {
  I_Instrument *instrument = getInstrument();
  if (!instrument) {
    return true;
  }

  const bool isLegacySampler = instrument->GetType() == IT_SAMPLE;
  NAssert(isLegacySampler || instrument->GetType() == IT_SAMPLE_SOURCE);
  Variable *start = instrument->FindVariable(
      isLegacySampler ? FourCC::SampleInstrumentStart
                      : FourCC::SampleSourceInstrumentStart);
  Variable *loopStart = instrument->FindVariable(
      isLegacySampler ? FourCC::SampleInstrumentLoopStart
                      : FourCC::SampleSourceInstrumentLoopStart);
  Variable *end = instrument->FindVariable(
      isLegacySampler ? FourCC::SampleInstrumentEnd
                      : FourCC::SampleSourceInstrumentEnd);
  NAssert(start && loopStart && end);

  bool hasSlices = false;
  int32_t sampleSize = 0;
  if (isLegacySampler) {
    SampleInstrument *sampleInstrument =
        static_cast<SampleInstrument *>(instrument);
    hasSlices = sampleInstrument->HasSlicesForWarning();
    sampleSize = sampleInstrument->GetSampleSize();
  } else {
    SampleSourceInstrument *sampleSourceInstrument =
        static_cast<SampleSourceInstrument *>(instrument);
    hasSlices = sampleSourceInstrument->HasSlicesForWarning();
    sampleSize = sampleSourceInstrument->GetSampleSize();
  }

  const bool hasEdits = hasSlices || start->GetInt() != 0 ||
                        loopStart->GetInt() != 0 || end->GetInt() != sampleSize;
  if (!hasEdits) {
    return true;
  }

  pendingSampleChangeInstrument_ = instrument;
  pendingSampleChangeNewIndex_ = newSampleIndex;

  MessageBox *message = MessageBox::Create(*this, "Change sample &",
                                           "reset edits?", MBBF_YES | MBBF_NO);
  DoModal(
      message,
      ModalViewCallback::create<InstrumentView,
                                &InstrumentView::onConfirmSampleChange>(*this));
  return false;
}

void InstrumentView::updateSamplePositionFieldLimits(I_Instrument *instrument,
                                                     int32_t sampleIndex) {
  if (!instrument) {
    return;
  }

  int32_t sampleSize = 0;
  SamplePool *pool = SamplePool::GetInstance();
  if (sampleIndex >= 0 && sampleIndex < pool->GetNameListSize()) {
    SoundSource *source = pool->GetSource(static_cast<uint32_t>(sampleIndex));
    if (source && !source->IsMulti()) {
      sampleSize = source->GetSize(-1);
    }
  }
  const int32_t maximum = sampleSize > 0 ? sampleSize - 1 : 0;

  // These fields outlive a sample selection change, so refresh the limits
  // captured when fillSampleParameters() built them.
  for (auto &field : bigHexVarField_) {
    const FourCC id = field.GetVariableID();
    const bool isSamplePosition =
        instrument->GetType() == IT_SAMPLE
            ? id == FourCC::SampleInstrumentStart ||
                  id == FourCC::SampleInstrumentLoopStart ||
                  id == FourCC::SampleInstrumentEnd
            : instrument->GetType() == IT_SAMPLE_SOURCE &&
                  (id == FourCC::SampleSourceInstrumentStart ||
                   id == FourCC::SampleSourceInstrumentLoopStart ||
                   id == FourCC::SampleSourceInstrumentEnd);
    if (isSamplePosition) {
      field.SetMaximum(maximum);
    }
  }
}

void InstrumentView::fillMidiParameters() {

  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(i);
  MidiInstrument *instrument = (MidiInstrument *)instr;
  GUIPoint position = GetAnchor();

  // offset y to account for instrument type, name and export/import fields
  position._y += 3;

  Variable *v = instrument->FindVariable(FourCC::MidiInstrumentChannel);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Channel: %2.2d", 0, 0x0F, 1, 0x04, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentVolume);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Volume: %2.2X", 0, 0xFF, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentNoteLength);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Length: %2.2X", 0, 0xFF, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentProgram);
  intVarOffField_.emplace_back(
      UIIntVarOffField(position, *v, "Program: %2.2X", 0, 0x7F, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentTableAutomation);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Automation: %s", 0, 1, 1, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentTable);
  intVarOffField_.emplace_back(
      UIIntVarOffField(position, *v, "Table: %2.2X", 0, 0x7F, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));
};

void InstrumentView::fillStarLoomParameters() {
  static_assert(StarLoomInstrumentControls::kVisibleParameters.size() == 13);
  StarLoomInstrument *instrument =
      static_cast<StarLoomInstrument *>(getInstrument());
  const GUIPoint anchor = GetAnchor();
  const int labelX = anchor._x;
  const int oscillatorX[2] = {12, 22};
  const FourCC shapeIDs[2] = {FourCC::StarLoomInstrumentShape,
                              FourCC::StarLoomInstrumentOscillator2Shape};
  const FourCC pitchOffsetIDs[2] = {
      FourCC::StarLoomInstrumentOscillator1PitchOffset,
      FourCC::StarLoomInstrumentOscillator2PitchOffset};
  const FourCC timbreIDs[2] = {FourCC::StarLoomInstrumentTimbre,
                               FourCC::StarLoomInstrumentOscillator2Timbre};
  const FourCC colorIDs[2] = {FourCC::StarLoomInstrumentColor,
                              FourCC::StarLoomInstrumentOscillator2Color};

  const auto addSectionHeader = [this](int x, int y, const char *label) {
    staticField_.emplace_back(GUIPoint(x, y), label, CD_HILITE2);
    fieldList_.insert(fieldList_.end(), &staticField_.back());
  };
  const auto addOscillatorRow = [this, instrument, labelX, &oscillatorX](
                                    int y, const char *label, const FourCC *ids,
                                    const char *format, int min, int max,
                                    int xOffset, int yOffset) {
    staticField_.emplace_back(GUIPoint(labelX, y), label);
    fieldList_.insert(fieldList_.end(), &staticField_.back());
    for (size_t oscillator = 0; oscillator < 2; ++oscillator) {
      Variable *variable = instrument->FindVariable(ids[oscillator]);
      intVarField_.emplace_back(GUIPoint(oscillatorX[oscillator], y), *variable,
                                format, min, max, xOffset, yOffset);
      fieldList_.insert(fieldList_.end(), &intVarField_.back());
    }
  };

  const int commonY = anchor._y + 2;
  Variable *variable = instrument->FindVariable(FourCC::StarLoomInstrumentMix);
  intVarField_.emplace_back(GUIPoint(labelX, commonY), *variable,
                            "Mix:      %s", -0x7F, 0x7F, 1, 0x10, 0,
                            UIIntVarFieldDisplayMode::SignedHex);
  fieldList_.insert(fieldList_.end(), &intVarField_.back());

  variable =
      instrument->FindVariable(FourCC::StarLoomInstrumentInteractionMode);
  intVarField_.emplace_back(
      GUIPoint(labelX, commonY + 1), *variable, "Interact: %s", 0,
      StarLoomInstrumentDSP::kInteractionModeCount - 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &intVarField_.back());

  variable = instrument->FindVariable(FourCC::StarLoomInstrumentSignature);
  intVarField_.emplace_back(GUIPoint(labelX, commonY + 2), *variable,
                            "Signature: %s", 0, 4, 1, 1);
  fieldList_.insert(fieldList_.end(), &intVarField_.back());

  const int voiceHeaderY = commonY + 4;
  addSectionHeader(labelX, voiceHeaderY, "Voices");
  addOscillatorRow(voiceHeaderY + 1, "Shape:", shapeIDs, "%-8s", 0,
                   StarLoomInstrumentShapes::kAllowedShapeCount - 1, 1, 1);
  addOscillatorRow(voiceHeaderY + 2, "Timbre:", timbreIDs, "%2.2X", 0, 0xFF, 1,
                   0x10);
  addOscillatorRow(voiceHeaderY + 3, "Color:", colorIDs, "%2.2X", 0, 0xFF, 1,
                   0x10);
  staticField_.emplace_back(GUIPoint(labelX, voiceHeaderY + 4), "Tune:");
  fieldList_.insert(fieldList_.end(), &staticField_.back());
  for (size_t oscillator = 0; oscillator < 2; ++oscillator) {
    Variable *variable = instrument->FindVariable(pitchOffsetIDs[oscillator]);
    pitchOffsetVarField_.emplace_back(
        GUIPoint(oscillatorX[oscillator], voiceHeaderY + 4), *variable);
    fieldList_.insert(fieldList_.end(), &pitchOffsetVarField_.back());
  }

  const int tableY = voiceHeaderY + 6;
  Variable *table = instrument->FindVariable(FourCC::StarLoomInstrumentTable);
  intVarOffField_.emplace_back(GUIPoint(labelX, tableY), *table, "Table: %2.2X",
                               0x00, TABLE_COUNT - 1, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &intVarOffField_.back());

  Variable *automation =
      instrument->FindVariable(FourCC::StarLoomInstrumentTableAutomation);
  intVarField_.emplace_back(GUIPoint(labelX + 12, tableY), *automation,
                            "Auto: %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &intVarField_.back());
}

void InstrumentView::fillFM6Parameters() {
  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(i);
  FM6Instrument *instrument = static_cast<FM6Instrument *>(instr);
  GUIPoint position = GetAnchor();

  position._y += 2;
  fm6ActionField_.emplace_back("Presets", FourCC::ActionShowFM6Presets,
                               position);
  fm6ActionField_.back().AddObserver(*this);
  fieldList_.insert(fieldList_.end(), &fm6ActionField_.back());

  position._y += 1;
  fm6AlgoField_.emplace_back(position, *instrument, fm6Selection_);
  fm6AlgoField_.back().AddObserver(*this);
  fieldList_.insert(fieldList_.end(), &fm6AlgoField_.back());

  // The panel below fills the rows between the algorithm row and the table
  // row, showing whichever block the algorithm field's cursor selects. Every
  // operator uses the same layout, so moving the cursor between operators
  // only needs a redraw; switching to or from the global block rebuilds.
  if (fm6Selection_ >= FM6SelectionGlobal) {
    fillFM6GlobalPanel(*instrument, position._x, position._y + 1);
  } else {
    fillFM6OperatorPanel(*instrument, position._x, position._y + 1);
  }

  // Clear the 12 rows the panel occupies: a leading blank, then three
  // sections of a blank-separated heading plus its value rows and then
  // a blank line
  position._y += 14;
  Variable *v = instrument->FindVariable(FourCC::FM6InstrumentTable);
  intVarOffField_.emplace_back(position, *v, "Table: %2.2X", 0x00,
                               TABLE_COUNT - 1, 1, 0x10);
  intVarOffField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));

  position._x += 13;
  v = instrument->FindVariable(FourCC::FM6InstrumentTableAutomation);
  intVarField_.emplace_back(position, *v, "Auto: %s", 0, 1, 1, 1);
  intVarField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
}

void InstrumentView::addFM6Label(int x, int y, const char *text) {
  staticField_.emplace_back(GUIPoint(x, y), text);
  fieldList_.insert(fieldList_.end(), &staticField_.back());
}

// Section headings follow the same convention as the SID screen: the plain
// section name in CD_HILITE2, with no rule drawn around it.
void InstrumentView::addFM6SectionHeader(int x, int y, const char *text) {
  staticField_.emplace_back(GUIPoint(x, y), text, CD_HILITE2);
  fieldList_.insert(fieldList_.end(), &staticField_.back());
}

void InstrumentView::addFM6Param(int x, int y, FM6Instrument &instrument,
                                 const FM6ParamDef &def) {
  fm6ParamField_.emplace_back(GUIPoint(x, y), instrument, fm6Selection_, def);
  fieldList_.insert(fieldList_.end(), &fm6ParamField_.back());
}

void InstrumentView::fillFM6OperatorPanel(FM6Instrument &instrument, int x,
                                          int y) {
  addFM6SectionHeader(x, y + 1, "Envelope");
  addFM6Label(x, y + 2, "Rate:");
  addFM6Label(x, y + 3, "Level:");
  for (int i = 0; i < 4; ++i) {
    addFM6Param(x + 6 + (i * 4), y + 2, instrument, kFM6OpRate[i]);
    addFM6Param(x + 6 + (i * 4), y + 3, instrument, kFM6OpLevel[i]);
  }

  addFM6SectionHeader(x, y + 5, "Scaling");
  addFM6Label(x, y + 6, "Break:");
  addFM6Param(x + 6, y + 6, instrument, kFM6OpBreakPoint);
  addFM6Label(x + 12, y + 6, "Depth:");
  addFM6Param(x + 18, y + 6, instrument, kFM6OpLeftDepth);
  addFM6Label(x + 20, y + 6, "/");
  addFM6Param(x + 21, y + 6, instrument, kFM6OpRightDepth);
  addFM6Label(x, y + 7, "Curve:");
  addFM6Param(x + 6, y + 7, instrument, kFM6OpLeftCurve);
  addFM6Label(x + 10, y + 7, "/");
  addFM6Param(x + 11, y + 7, instrument, kFM6OpRightCurve);
  addFM6Label(x + 17, y + 7, "Rs:");
  addFM6Param(x + 20, y + 7, instrument, kFM6OpRateScaling);

  addFM6SectionHeader(x, y + 9, "Oscillator");
  staticField_.emplace_back(GUIPoint(x + 12, y + 9), fm6FixedFrequencyLabel_,
                            CD_EMPHASIS);
  fieldList_.insert(fieldList_.end(), &staticField_.back());
  addFM6Label(x, y + 10, "Mode:");
  addFM6Param(x + 5, y + 10, instrument, kFM6OpFixedMode);
  addFM6Label(x + 12, y + 10, "Crs:");
  addFM6Param(x + 16, y + 10, instrument, kFM6OpCoarse);
  addFM6Label(x + 19, y + 10, "Fine:");
  addFM6Param(x + 24, y + 10, instrument, kFM6OpFine);
  addFM6Label(x, y + 11, "Out:");
  addFM6Param(x + 4, y + 11, instrument, kFM6OpOutputLevel);
  addFM6Label(x + 8, y + 11, "Det:");
  addFM6Param(x + 12, y + 11, instrument, kFM6OpDetune);
  addFM6Label(x + 15, y + 11, "Vel:");
  addFM6Param(x + 19, y + 11, instrument, kFM6OpVelSens);
  addFM6Label(x + 21, y + 11, "AM:");
  addFM6Param(x + 24, y + 11, instrument, kFM6OpAmpModSens);
}

void InstrumentView::updateFM6FixedFrequencyLabel() {
  fm6FixedFrequencyLabel_[0] = '\0';

  I_Instrument *instrument = getInstrument();
  if (!instrument || instrument->GetType() != IT_FM6 || fm6Selection_ == 0U ||
      fm6Selection_ >= FM6SelectionGlobal) {
    return;
  }

  FM6Instrument *fm6Instrument = static_cast<FM6Instrument *>(instrument);
  const size_t sliceIndex = fm6::OperatorCount - fm6Selection_;
  const uint8_t modeAndCoarse =
      fm6Instrument->GetPatchByte(sliceIndex, kFM6OpFixedMode.offset);
  const uint8_t mode =
      (modeAndCoarse >> kFM6OpFixedMode.shift) & kFM6OpFixedMode.valueMask;
  if (mode == 0U) {
    return;
  }

  const uint8_t coarse =
      (modeAndCoarse >> kFM6OpCoarse.shift) & kFM6OpCoarse.valueMask;
  const uint8_t fineByte =
      fm6Instrument->GetPatchByte(sliceIndex, kFM6OpFine.offset);
  const uint8_t fine = (fineByte >> kFM6OpFine.shift) & kFM6OpFine.valueMask;
  const uint32_t milliHertz = fm6::Dx7FixedFrequencyMilliHertz(coarse, fine);
  formatFM6FixedFrequency(milliHertz, coarse, fm6FixedFrequencyLabel_,
                          sizeof(fm6FixedFrequencyLabel_));
}

void InstrumentView::fillFM6GlobalPanel(FM6Instrument &instrument, int x,
                                        int y) {
  addFM6SectionHeader(x, y + 1, "Pitch EG");
  addFM6Label(x, y + 2, "Rate:");
  addFM6Label(x, y + 3, "Level:");
  for (int i = 0; i < 4; ++i) {
    addFM6Param(x + 6 + (i * 4), y + 2, instrument, kFM6PitchEgRate[i]);
    addFM6Param(x + 6 + (i * 4), y + 3, instrument, kFM6PitchEgLevel[i]);
  }

  addFM6SectionHeader(x, y + 5, "LFO");
  addFM6Label(x, y + 6, "Wave:");
  addFM6Param(x + 5, y + 6, instrument, kFM6LfoWave);
  addFM6Label(x + 11, y + 6, "Sync:");
  addFM6Param(x + 16, y + 6, instrument, kFM6LfoSync);
  addFM6Label(x, y + 7, "Spd:");
  addFM6Param(x + 4, y + 7, instrument, kFM6LfoSpeed);
  addFM6Label(x + 8, y + 7, "Dly:");
  addFM6Param(x + 12, y + 7, instrument, kFM6LfoDelay);
  addFM6Label(x + 16, y + 7, "PMD:");
  addFM6Param(x + 20, y + 7, instrument, kFM6LfoPitchDepth);
  addFM6Label(x, y + 8, "AMD:");
  addFM6Param(x + 4, y + 8, instrument, kFM6LfoAmpDepth);
  addFM6Label(x + 8, y + 8, "PMS:");
  addFM6Param(x + 12, y + 8, instrument, kFM6PitchModSens);
  addFM6Label(x + 16, y + 8, "OscSy:");
  addFM6Param(x + 22, y + 8, instrument, kFM6OscSync);

  addFM6SectionHeader(x, y + 10, "Voice");
  addFM6Label(x, y + 11, "Feedback:");
  addFM6Param(x + 9, y + 11, instrument, kFM6Feedback);
  addFM6Label(x + 13, y + 11, "Transp:");
  addFM6Param(x + 20, y + 11, instrument, kFM6Transpose);
}

void InstrumentView::warpToNext(int offset) {
  int instrument = viewData_->currentInstrumentID_ + offset;
  if (instrument >= MAX_INSTRUMENT_COUNT) {
    instrument = instrument - MAX_INSTRUMENT_COUNT;
  };
  if (instrument < 0) {
    instrument = MAX_INSTRUMENT_COUNT + instrument;
  };
  viewData_->currentInstrumentID_ = instrument;
  onInstrumentChange();
  isDirty_ = true;
};

void InstrumentView::ProcessButtonMask(unsigned short mask, bool pressed) {

  if (!pressed) {
    const bool previewHeld =
        previewMode_ == PreviewMode::Parameter
            ? (mask & EPBM_ENTER) != 0
            : IsInstrumentPreviewComboHeld(mask, EPBM_PLAY, EPBM_ALT);
    if (previewMode_ != PreviewMode::None && !previewHeld) {
      stopInstrumentPreview();
      return;
    }
    if (embeddedSlicer_.ProcessButtonMask(*this, *viewData_, mask, pressed)) {
      return;
    }
    return;
  }

  isDirty_ = false;
  const bool embeddedSlicerActive = isEmbeddedSlicerActive();

  if (startInstrumentPreview(mask)) {
    return;
  }

  uint32_t focusSample = 0;
  embeddedSlicer_.SetFocusSampleHint(
      focusedEmbeddedSlicerPointSample(focusSample), focusSample);
  embeddedSlicer_.SetPreviewFocusHint(focusedEmbeddedSlicerPreviewField());
  updateEmbeddedSlicerPreviewRange();
  if (embeddedSlicer_.ProcessButtonMask(*this, *viewData_, mask, pressed)) {
    return;
  }

  if ((mask & EPBM_EDIT) && (mask & EPBM_ENTER)) {
    int i = viewData_->currentInstrumentID_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instr = bank->GetInstrument(i);
    if (GetFocus() == *fieldList_.begin()) {
      bool instrumentModified = checkInstrumentModified();
      if (instrumentModified) {
        MessageBox *mb = MessageBox::Create(*this, "Reset all settings?",
                                            MBBF_YES | MBBF_NO);
        // Live overlay backing preserves the waveform and text while the
        // dialog is open; opening it does not invalidate the base screen.
        pendingPurgeInstrument_ = instr;
        DoModal(mb,
                ModalViewCallback::create<
                    InstrumentView, &InstrumentView::onConfirmResetInstrument>(
                    *this));
      }
      return;
    }
    if (getInstrument()->GetType() == IT_SAMPLE ||
        getInstrument()->GetType() == IT_SAMPLE_SOURCE) {
      UIField *focus = GetFocus();
      UIIntVarField *field =
          (focus && focus->HasVariable()) ? (UIIntVarField *)focus : nullptr;
      Variable *endVar = nullptr;
      int sampleSize = 0;
      if (instr->GetType() == IT_SAMPLE) {
        SampleInstrument *sampleInstrument =
            static_cast<SampleInstrument *>(instr);
        endVar = sampleInstrument->FindVariable(FourCC::SampleInstrumentEnd);
        sampleSize = sampleInstrument->GetSampleSize();
      } else if (instr->GetType() == IT_SAMPLE_SOURCE) {
        SampleSourceInstrument *sampleSourceInstrument =
            static_cast<SampleSourceInstrument *>(instr);
        endVar = sampleSourceInstrument->FindVariable(
            FourCC::SampleSourceInstrumentEnd);
        sampleSize = sampleSourceInstrument->GetSampleSize();
      }
      if (endVar && field && field->GetVariableID() == endVar->GetID()) {
        Variable &var = field->GetVariable();
        var.SetInt(sampleSize - 1);
        isDirty_ = true;
        return;
      }
    }
  }

  // Call the parent class's implementation first to ensure action fields like
  // Export, Import work correctly
  pendingSampleSourceLoopModeDirection_ = 0;
  if ((mask & EPBM_ENTER) &&
      lastFocusID_ == FourCC::SampleSourceInstrumentLoopMode) {
    if ((mask & EPBM_UP) || (mask & EPBM_RIGHT)) {
      pendingSampleSourceLoopModeDirection_ = 1;
    }
    if ((mask & EPBM_DOWN) || (mask & EPBM_LEFT)) {
      pendingSampleSourceLoopModeDirection_ = -1;
    }
  }
  const bool fm6WasGlobal = fm6Selection_ >= FM6SelectionGlobal;
  FieldView::ProcessButtonMask(mask, pressed);
  pendingSampleSourceLoopModeDirection_ = 0;

  // ENTER+LEFT/RIGHT on the focused algorithm field can move its cursor
  // between an operator and the global block, which changes which parameter
  // fields exist. Rebuilding out here rather than inside the field's own
  // handler keeps the field alive for the rest of the input dispatch.
  if (!fm6AlgoField_.empty() &&
      fm6WasGlobal != (fm6Selection_ >= FM6SelectionGlobal)) {
    // Drop the focus pointer before the fields behind it are destroyed.
    ClearFocus();
    refreshInstrumentFields();
    if (!fm6AlgoField_.empty()) {
      SetFocus(&fm6AlgoField_.back());
    }
    isDirty_ = true;
  }
  if (embeddedSlicerActive) {
    uint32_t pointSample = 0;
    bool hasPointSample = focusedEmbeddedSlicerPointSample(pointSample);
    embeddedSlicer_.SetFocusSampleHint(hasPointSample, pointSample);
    embeddedSlicer_.SetPreviewFocusHint(focusedEmbeddedSlicerPreviewField());
    updateEmbeddedSlicerPreviewRange();
    if (hasPointSample && embeddedSlicer_.UpdateZoomWindow()) {
      embeddedSlicer_.InvalidateWaveform();
      embeddedSlicer_.Graph().RequestFullRedraw();
      embeddedSlicer_.UpdateLabels();
      isDirty_ = true;
    }
  }

  Player *player = Player::GetInstance();

  if (mask == EPBM_ENTER) {
    // Get the current field to check if we're on the sample field
    UIField *focus = GetFocus();
    UIIntVarField *currentField =
        (focus && focus->HasVariable()) ? (UIIntVarField *)focus : nullptr;

    // Only allow sample import when the sample field is selected
    Variable *sampleVar = nullptr;
    if (getInstrument()->GetType() == IT_SAMPLE) {
      SampleInstrument *sampleInstrument =
          static_cast<SampleInstrument *>(getInstrument());
      sampleVar =
          sampleInstrument->FindVariable(FourCC::SampleInstrumentSample);
    } else if (getInstrument()->GetType() == IT_SAMPLE_SOURCE) {
      SampleSourceInstrument *sampleSourceInstrument =
          static_cast<SampleSourceInstrument *>(getInstrument());
      sampleVar = sampleSourceInstrument->FindVariable(
          FourCC::SampleSourceInstrumentSample);
    }
    if (sampleVar && currentField &&
        currentField->GetVariableID() == sampleVar->GetID()) {

      if (viewMode_ == VM_NEW) {
        viewMode_ = VM_NORMAL; // clear the "enter double tap" state

        // First check if the samplelib exists
        bool samplelibExists =
            FileSystem::GetInstance()->exists(SAMPLES_LIB_DIR);

        if (!samplelibExists) {
          MessageBox *mb =
              MessageBox::Create(*this, "Can't access the samplelib", MBBF_OK);
          DoModal(
              mb,
              ModalViewCallback::create<InstrumentView,
                                        &InstrumentView::onSimpleModalDismiss>(
                  *this));
        } else {
          ImportView::SetSourceViewType(VT_INSTRUMENT);

          // Go to import sample
          ViewType vt = VT_IMPORT;
          ViewEvent ve(VET_SWITCH_VIEW, &vt);
          SetChanged();
          NotifyObservers(&ve);
        }
      } else {
        // mark as "new" mode so a 2nd following ENTER will trigger the sample
        // import above
        viewMode_ = VM_NEW;
      }
    } else if (viewMode_ == VM_NEW) {
      // If we're not on the sample field but in VM_NEW mode, reset it
      viewMode_ = VM_NORMAL;
    }

    UIIntVarField *field = currentField;
    if (field) {
      Variable &v = field->GetVariable();
      switch (v.GetID()) {
      case FourCC::SampleInstrumentTable:
      case FourCC::SampleSourceInstrumentTable: {
        int next = TableHolder::GetInstance()->GetNext();
        if (next != NO_MORE_TABLE) {
          v.SetInt(next);
          isDirty_ = true;
        }
        break;
      }
      default:
        break;
      }
    }
    mask &= (0xFFFF - EPBM_ENTER);
  } else {
    // Clear the VM_NEW state if any key other than ENTER is pressed
    if (viewMode_ == VM_NEW) {
      viewMode_ = VM_NORMAL;
    }
  }

  if (viewMode_ == VM_CLONE) {
    if ((mask & EPBM_ENTER) && (mask & EPBM_ALT)) {
      UIField *focus = GetFocus();
      UIIntVarField *field =
          (focus && focus->HasVariable()) ? (UIIntVarField *)focus : nullptr;
      mask &= (0xFFFF - EPBM_ENTER);
      if (!field) {
        return;
      }
      Variable &v = field->GetVariable();
      int current = v.GetInt();
      if (current == -1)
        return;

      if ((field->GetVariableID() == FourCC::SampleInstrumentTable) ||
          (field->GetVariableID() == FourCC::SampleSourceInstrumentTable) ||
          (field->GetVariableID() == FourCC::MidiInstrumentTable)) {
        int next = TableHolder::GetInstance()->Clone(current);
        if (next != NO_MORE_TABLE) {
          v.SetInt(next);
          isDirty_ = true;
        }
      };
    }
    mask &= (0xFFFF - (EPBM_ENTER | EPBM_ALT));
  };

  if (viewMode_ == VM_SELECTION) {
  } else {
    // viewMode_ = VM_NORMAL;
  }

  // EDIT Modifier
  if (mask & EPBM_EDIT) {
    if (mask & EPBM_LEFT)
      warpToNext(-1);
    if (mask & EPBM_RIGHT)
      warpToNext(+1);
    if (mask & EPBM_DOWN)
      warpToNext(-16);
    if (mask & EPBM_UP)
      warpToNext(+16);
    if (mask & EPBM_ALT) {
      viewMode_ = VM_CLONE;
    };
    if (mask & EPBM_PLAY) {
      // recording screen
      if (!Player::GetInstance()->IsRunning()) {
        switchToRecordView();
      }
    }
  } else if (mask & EPBM_NAV) {
    // NAV Modifier
    if (mask & EPBM_LEFT) {
      ViewType vt = VT_PHRASE;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);

      // remove listening when leaving this screen
      getInstrument()->RemoveObserver(*this);
      ((WatchedVariable *)&instrumentType_)->RemoveObserver(*this);

      SetChanged();
      NotifyObservers(&ve);
    }

    if ((mask & EPBM_RIGHT) && SupportsAudioVoiceScreen(getInstrument())) {
      ViewType vt = VT_VOICE;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }

    if (mask & EPBM_DOWN) {

      // Go to table view

      ViewType vt = VT_TABLE2;

      int i = viewData_->currentInstrumentID_;
      InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
      I_Instrument *instr = bank->GetInstrument(i);
      int table = instr->GetTable();
      if (table != VAR_OFF) {
        viewData_->currentTable_ = table;
      }
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }

    if (mask & EPBM_PLAY) {
      player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                            viewData_->chainRow_);
    }
  } else {
    // No modifier
    if (mask & EPBM_PLAY) {
      player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                            viewData_->chainRow_);
    }
  }

  UIField *focus = GetFocus();
  lastFocusWasSliceGraph_ = embeddedSlicer_.HasGraphFocus(focus);
  UIIntVarField *field =
      (focus && focus->HasVariable()) ? (UIIntVarField *)focus : nullptr;
  if (field) {
    lastFocusID_ = field->GetVariableID();
  }
};

void InstrumentView::DrawView() {
  updateFM6FixedFrequencyLabel();
  Clear();

  GUITextProperties props;

  // Draw title
  etl::string<23> title("Instrument ");
  etl::format_spec format;
  format.width(2).fill('0').hex().upper_case(true);
  etl::to_string(viewData_->currentInstrumentID_, title, format, true);
  setTitle(title);
  // Draw fields
  FieldView::Redraw();
  drawEmbeddedSlicerWaveform();
  drawMap();

  I_Instrument *instr = getInstrument();
  if (instr) {
    InstrumentType type = instr->GetType();
  }
}

void InstrumentView::AnimationUpdate() {
  GUITextProperties props;
  drawTitlebar(TEXT_REGION_HEADER, props);
  drawPowerButtonUI(props);
  embeddedSlicer_.AnimationUpdate(*this);
  if (isDirty_) {
    ((AppWindow &)w_).SetDirty();
  }
}

void InstrumentView::OnFocus() {
  Trace::Log("INSTRUMENTVIEW", "onFocus");

  // Importing a legacy sample instrument can enable it after the view's last
  // reset, so refresh the rotation limit whenever the screen is entered.
  updateInstrumentTypeSelectionAvailability();

  // Get latest selected instrument, ensures we display the instrument that was
  // selected in the PhraseView
  int currentID = viewData_->currentInstrumentID_;
  Trace::Debug("INSTRUMENTVIEW", "Current instrument ID from ViewData: %d",
               currentID);

  // Get the current instrument based on the ViewData's currentInstrumentID_
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(currentID);

  if (instr) {
    // Update the instrument type field to match the current instrument
    InstrumentType currentType = instr->GetType();

    Trace::Debug("INSTRUMENTVIEW", "Current instrument type: %d", currentType);

    // Only update if the type has changed
    int currentTypeIndex = displayIndexFromInstrumentType(currentType);
    if (instrumentType_.GetInt() != currentTypeIndex) {
      Trace::Log("INSTRUMENTVIEW",
                 "OnFocus instrument type changed from %d to %d",
                 instrumentType_.GetInt(), currentType);
      // Set the instrument type without triggering the observer update
      // because we dont want the observer to do its normal check for a modified
      // instrument
      instrumentType_.SetInt(currentTypeIndex, false);
    }

    // Always refresh the UI fields when focusing the view in case of instrument
    // change from last time
    onInstrumentTypeChange(true);
  }
}

void InstrumentView::Update(Observable &o, I_ObservableData *data) {

  if (!hasFocus_) {
    return;
  }

  uintptr_t fourcc = (uintptr_t)data;

  switch (fourcc) {
  case FourCC::SampleInstrumentSlices:
  case FourCC::SampleInstrumentStart:
    if (isEmbeddedSlicerActive() &&
        embeddedSlicer_.HandleVariableUpdate(*this, fourcc)) {
      ((AppWindow &)w_).SetDirty();
    }
    break;
  case FourCC::SampleSourceInstrumentStart:
  case FourCC::SampleSourceInstrumentLoopStart:
  case FourCC::SampleSourceInstrumentEnd:
    if (isEmbeddedSlicerActive()) {
      uint32_t sample = 0;
      embeddedSlicer_.SetFocusSampleHint(
          focusedEmbeddedSlicerPointSample(sample), sample);
      embeddedSlicer_.SetPreviewFocusHint(focusedEmbeddedSlicerPreviewField());
      updateEmbeddedSlicerPreviewRange();
      if (embeddedSlicer_.UpdateZoomWindow()) {
        embeddedSlicer_.InvalidateWaveform();
      }
      embeddedSlicer_.Graph().RequestFullRedraw();
      isDirty_ = true;
      ((AppWindow &)w_).SetDirty();
    }
    break;
  case FourCC::SampleSourceInstrumentLoopMode:
    if (isEmbeddedSlicerActive()) {
      updateEmbeddedSlicerPreviewRange();
      embeddedSlicer_.Graph().RequestFullRedraw();
      isDirty_ = true;
      ((AppWindow &)w_).SetDirty();
    }
    if (skipSampleSourceOscillatorIfSampleTooLarge(
            getInstrument(), pendingSampleSourceLoopModeDirection_)) {
      isDirty_ = true;
      ((AppWindow &)w_).SetDirty();
    }
    break;
  case FourCC::ActionAutoSlice:
    if (isEmbeddedSlicerActive()) {
      if (embeddedSlicer_.HasSlicesForPlayback()) {
        MessageBox *mb = MessageBox::Create(*this, "Replace current slices?",
                                            MBBF_YES | MBBF_NO);
        DoModal(mb, ModalViewCallback::create<
                        &InstrumentView::AutoSliceConfirmCallback>());
      } else {
        embeddedSlicer_.AutoSliceEvenly(*this);
        updateSliceCountLabel(sliceCountLabel_, getInstrument());
        ((AppWindow &)w_).SetDirty();
      }
    }
    break;
  case FourCC::VarInstrumentType: {
    // Get the current instrument to determine its actual type
    I_Instrument *instr = getInstrument();
    InstrumentType currentType = instr ? instr->GetType() : IT_NONE;

    // Store the proposed instrument type BEFORE we revert the UI
    InstrumentType proposedType =
        kDisplayIndexToInstrumentType[instrumentType_.GetInt()];

    // Revert the UI field back to the current type until confirmed
    instrumentType_.SetInt(displayIndexFromInstrumentType(currentType), false);

    Player *player = Player::GetInstance();
    const bool changeBlockedByPlayback =
        player->IsRunning() &&
        !project_->CanChangeInstrumentTypeWhilePlaying(
            static_cast<uint8_t>(viewData_->currentInstrumentID_));
    if (!changeBlockedByPlayback) {
      // Check if any instrument field has been modified
      bool instrumentModified = checkInstrumentModified();
      if (instrumentModified) {
        MessageBox *mb = MessageBox::Create(
            *this, "Change Instrument &", "lose settings?", MBBF_YES | MBBF_NO);
        pendingInstrumentType_ = proposedType;
        DoModal(
            mb,
            ModalViewCallback::create<
                InstrumentView, &InstrumentView::onConfirmInstrumentTypeChange>(
                *this));
      } else {
        // Apply the proposed type change immediately if not modified
        instrumentType_.SetInt(displayIndexFromInstrumentType(proposedType),
                               false);
        onInstrumentTypeChange();
      }
    } else {
      MessageBox *mb = MessageBox::Create(*this, "Not while playing", MBBF_OK);
      DoModal(mb,
              ModalViewCallback::create<InstrumentView,
                                        &InstrumentView::onSimpleModalDismiss>(
                  *this));
    }
    break;
  }
  case FourCC::ActionExport: {
    handleInstrumentExport();
  } break;
  case FourCC::ActionImport: {
    // Switch to the InstrumentImportView
    InstrumentImportView::SetMode(InstrumentImportView::Mode::Instrument);
    ViewType vt = VT_INSTRUMENT_IMPORT;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  } break;
  case FourCC::ActionShowFM6Presets: {
    stopInstrumentPreview();
    ViewType vt = VT_FM6_PRESETS;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  } break;
  case FourCC::SampleInstrumentSample:
  case FourCC::SampleSourceInstrumentSample: {
    I_Instrument *instr = getInstrument();
    if (!instr || (instr->GetType() != IT_SAMPLE &&
                   instr->GetType() != IT_SAMPLE_SOURCE)) {
      break;
    }

    int newIndex = -1;
    if (instr->GetType() == IT_SAMPLE) {
      SampleInstrument *sampleInstr = static_cast<SampleInstrument *>(instr);
      newIndex = sampleInstr->GetSampleIndex();
    } else if (instr->GetType() == IT_SAMPLE_SOURCE) {
      SampleSourceInstrument *sampleSourceInstr =
          static_cast<SampleSourceInstrument *>(instr);
      newIndex = sampleSourceInstr->GetSampleIndex();
    }

    if (newIndex == lastSampleIndex_) {
      break;
    }

    updateSamplePositionFieldLimits(instr, newIndex);
    resetSampleSourceOscillatorIfSampleTooLarge(instr);
    lastSampleIndex_ = newIndex;
    updateSliceCountLabel(sliceCountLabel_, instr);
    if (instr->GetType() == IT_SAMPLE_SOURCE) {
      setupEmbeddedSlicer();
    }
    isDirty_ = true;
  } break;
  case FourCC::ActionShowSampleSlices: {
    I_Instrument *instr = getInstrument();
    if (!instr || (instr->GetType() != IT_SAMPLE &&
                   instr->GetType() != IT_SAMPLE_SOURCE)) {
      break;
    }
    int sampleIndex = -1;
    if (instr->GetType() == IT_SAMPLE) {
      SampleInstrument *sampleInstr = static_cast<SampleInstrument *>(instr);
      sampleIndex = sampleInstr->GetSampleIndex();
    } else if (instr->GetType() == IT_SAMPLE_SOURCE) {
      SampleSourceInstrument *sampleSourceInstr =
          static_cast<SampleSourceInstrument *>(instr);
      sampleIndex = sampleSourceInstr->GetSampleIndex();
    }
    if (sampleIndex < 0) {
      MessageBox *mb =
          MessageBox::Create(*this, "Assign a sample first", MBBF_OK);
      DoModal(mb,
              ModalViewCallback::create<InstrumentView,
                                        &InstrumentView::onSimpleModalDismiss>(
                  *this));
      break;
    }
    ViewType vt = VT_SAMPLE_SLICES;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  } break;
  case FourCC::MidiInstrumentProgram: {
    // Send program changes immediately, including while the sequencer is
    // stopped, so the connected MIDI instrument can be auditioned.
    I_Instrument *instr = getInstrument();
    if (instr && instr->GetType() == IT_MIDI) {
      MidiInstrument *midiInstr = (MidiInstrument *)instr;

      // Get the channel and program values
      Variable *channelVar =
          midiInstr->FindVariable(FourCC::MidiInstrumentChannel);
      Variable *programVar =
          midiInstr->FindVariable(FourCC::MidiInstrumentProgram);

      if (channelVar && programVar) {
        int channel = channelVar->GetInt();
        int program = programVar->GetInt();

        Config *config = Config::GetInstance();
        Variable *auditionVar =
            config ? config->FindVariable(FourCC::VarInstrumentAudition)
                   : nullptr;
        if (auditionVar && auditionVar->GetInt() == 1) {
          midiInstr->SendProgramChangeWithNote(channel, program);
        } else {
          midiInstr->SendProgramChange(channel, program);
        }
      }
    }
  } break;
  default:
    break;
  }
  auditionParameterChange(FourCC(static_cast<FourCC::value_type>(fourcc)));
}

bool InstrumentView::checkInstrumentModified() {
  // Get current instrument
  I_Instrument *instrument = getInstrument();
  if (!instrument) {
    return false;
  }

  // Get the list of variables for this instrument
  etl::ivector<Variable *> *variables = instrument->Variables();
  if (!variables) {
    return false;
  }

  // Check if any variable has been modified from its default value
  for (auto it = variables->begin(); it != variables->end(); ++it) {
    Variable *var = *it;
    if (var && var->IsModified()) {
      return true;
    }
  }

  // No variables have been modified
  return false;
}

void InstrumentView::resetInstrumentToDefaults() {
  // Get current instrument
  I_Instrument *instrument = getInstrument();
  if (!instrument) {
    return;
  }

  // Get the list of variables for this instrument
  etl::ivector<Variable *> *variables = instrument->Variables();
  if (!variables) {
    return;
  }

  // Reset all variables to their default values
  for (auto it = variables->begin(); it != variables->end(); ++it) {
    Variable *var = *it;
    if (var) {
      var->Reset();
    }
  }
}

void InstrumentView::handleInstrumentExport() {
  // Get current instrument using its id
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(
          viewData_->currentInstrumentID_);

  // Check if the instrument has a name set
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name = instrument->GetDisplayName();
  // Check if the name is empty, the default value, or matches the default
  // instrument type name
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> defaultTypeName =
      instrument->GetDefaultName();

  if (name.empty() || name == defaultTypeName) {
    // Show error message if no name is set
    MessageBox *mb = MessageBox::Create(*this, "Please set a name",
                                        "before exporting", MBBF_OK);
    DoModal(mb,
            ModalViewCallback::create<InstrumentView,
                                      &InstrumentView::onSimpleModalDismiss>(
                *this));
  } else {
    // Export the instrument using the name field
    PersistencyResult result =
        PersistencyService::GetInstance()->ExportInstrument(instrument, name);

    if (result == PERSIST_EXISTS) {
      // File already exists, ask user if they want to override it
      etl::string<sizeof("Overwrite existing file: ") - 1> confirmMsg =
          "Overwrite existing file?";
      MessageBox *mb = MessageBox::Create(*this, confirmMsg.c_str(),
                                          name.c_str(), MBBF_YES | MBBF_NO);

      exportInstrument_ = instrument;
      exportName_ = name;
      DoModal(
          mb,
          ModalViewCallback::create<InstrumentView,
                                    &InstrumentView::onConfirmExportOverwrite>(
              *this));
    } else {
      // Create a message with the instrument name
      etl::string<MAX_INSTRUMENT_NAME_LENGTH + sizeof("Exported: ") - 1>
          successMsg = "Exported: ";
      successMsg += name;

      const char *message = result == PERSIST_SAVED
                                ? successMsg.c_str()
                                : "Failed to export instrument";
      // Show export result message
      MessageBox *mb = MessageBox::Create(*this, message, MBBF_OK);
      DoModal(mb,
              ModalViewCallback::create<InstrumentView,
                                        &InstrumentView::onSimpleModalDismiss>(
                  *this));
    }
  }
}

void InstrumentView::onConfirmInstrumentTypeChange(View &, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    applyProposedTypeChangeUI();
  }
  isDirty_ = true;
}

void InstrumentView::onConfirmResetInstrument(View &, ModalView &dialog) {
  I_Instrument *instr = pendingPurgeInstrument_;
  pendingPurgeInstrument_ = nullptr;

  if (dialog.GetReturnCode() != MBL_YES || !instr) {
    // The live backing restores the waveform even when reset is declined.
    // Refresh the remaining fields after the dialog closes.
    isDirty_ = true;
    return;
  }

  instr->Purge();
  updateSliceCountLabel(sliceCountLabel_, instr);

  // A purge drops the sample, so the slicer must forget the waveform it
  // rendered for it and redraw the graph area empty.
  setupEmbeddedSlicer();
  isDirty_ = true;
}

void InstrumentView::onConfirmSampleChange(View &, ModalView &dialog) {
  I_Instrument *instrument = pendingSampleChangeInstrument_;
  int newIndex = pendingSampleChangeNewIndex_;
  pendingSampleChangeInstrument_ = nullptr;
  pendingSampleChangeNewIndex_ = -1;

  if (!instrument) {
    return;
  }

  if (dialog.GetReturnCode() == MBL_YES) {
    Variable *sample = nullptr;
    if (instrument->GetType() == IT_SAMPLE) {
      sample = instrument->FindVariable(FourCC::SampleInstrumentSample);
    } else {
      sample = instrument->FindVariable(FourCC::SampleSourceInstrumentSample);
    }
    NAssert(sample);

    sample->SetInt(newIndex);
    const bool isLegacySampler = instrument->GetType() == IT_SAMPLE;
    if (isLegacySampler) {
      static_cast<SampleInstrument *>(instrument)->ClearSlices();
    } else {
      static_cast<SampleSourceInstrument *>(instrument)->ClearSlices();
    }

    lastSampleIndex_ = newIndex;
    updateSamplePositionFieldLimits(instrument, newIndex);
    resetSampleSourceOscillatorIfSampleTooLarge(instrument);
    updateSliceCountLabel(sliceCountLabel_, instrument);
    if (instrument->GetType() == IT_SAMPLE_SOURCE) {
      setupEmbeddedSlicer();
    }
    isDirty_ = true;
    return;
  }

  isDirty_ = true;
}

void InstrumentView::onConfirmExportOverwrite(View &, ModalView &dialog) {
  I_Instrument *instrument = exportInstrument_;
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name = exportName_;
  exportInstrument_ = nullptr;
  exportName_.clear();

  if (dialog.GetReturnCode() == MBL_YES && instrument) {
    PersistencyService::GetInstance()->ExportInstrument(instrument, name, true);
    Trace::Log("INSTRUMENTVIEW", "Instrument '%s' exported with overwrite",
               name.c_str());
  }

  onSimpleModalDismiss(*this, dialog);
}

void InstrumentView::onSimpleModalDismiss(View &, ModalView &) {
  if (isEmbeddedSlicerActive()) {
    isDirty_ = true;
  }
}
