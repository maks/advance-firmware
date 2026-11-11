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

#include "ProjectView.h"
#include "Application/AppWindow.h"
#include "Application/Model/Scale.h"
#include "Application/Model/Song.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Utils/char.h"
#include "Application/Utils/randomnames.h"
#include "Application/Views/BaseClasses/BrowserPathUtils.h"
#include "Application/Views/ImportView.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/ModalDialogs/RenderProgressModal.h"
#include "Application/Views/SampleEditorView.h"
#include "Application/Views/SelectionResampleSupport.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UITempoField.h"
#include "BaseClasses/View.h"
#include "BaseClasses/ViewEvent.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include <etl/to_string.h>

static void SaveAsOverwriteCallback(View &v, ModalView &dialog);

ProjectMidiValueField::ProjectMidiValueField(const GUIPoint &position,
                                             uint8_t &value, uint8_t minimum,
                                             uint8_t maximum,
                                             uint8_t defaultValue)
    : UIField(position), value_(&value), minimum_(minimum), maximum_(maximum),
      defaultValue_(defaultValue) {}

void ProjectMidiValueField::Draw(GUIWindow &w, int offset) {
  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  char value[3];
  hex2char(*value_, value);
  if (focus_) {
    static_cast<AppWindow &>(w).SetColor(CD_HILITE2);
    props.invert_ = true;
  } else {
    static_cast<AppWindow &>(w).SetColor(CD_NORMAL);
  }
  w.DrawString(value, position, props);
}

void ProjectMidiValueField::ProcessArrow(unsigned short mask) {
  int16_t next = *value_;
  switch (mask) {
  case EPBM_UP:
    next += 0x10;
    break;
  case EPBM_DOWN:
    next -= 0x10;
    break;
  case EPBM_LEFT:
    next -= 0x01;
    break;
  case EPBM_RIGHT:
    next += 0x01;
    break;
  }

  next = std::clamp(next, static_cast<int16_t>(minimum_),
                    static_cast<int16_t>(maximum_));
  *value_ = static_cast<uint8_t>(next);
}

void ProjectMidiValueField::ProcessClear() { *value_ = defaultValue_; }

uint8_t ProjectMidiValueField::GetValue() const { return *value_; }

static bool SaveAsProject(ProjectView &projectView) {
  PersistencyService *persist = PersistencyService::GetInstance();
  const auto projName = projectView.getProjectName();
  const auto oldProjName = projectView.getOldProjectName();

  if (persist->Save(projName.c_str(), oldProjName.c_str(), true) !=
      PERSIST_SAVED) {
    Trace::Error("failed to save renamed project %s [old: %s]",
                 projName.c_str(), oldProjName.c_str());
    return false;
  }

  if (persist->SaveProjectState(projName.c_str()) != PERSIST_SAVED) {
    Trace::Error("Failed to save project state");
    return false;
  } else {
    if (!oldProjName.empty() && oldProjName != projName) {
      persist->ClearAutosave(oldProjName.c_str());
    }
    Trace::Log("PROJECTVIEW-STATIC", "SAVED_AS:%s", projName.c_str());
    projectView.clearSaveAsFlag();
    // Update the app's original directory name so future operations use it
    AppWindow::GetInstance()->originalProjectName_ = projName;
  }
  return true;
}

static void CreateNewProjectCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    // We are abandoning the current project, so clear its autosave to avoid
    // orphaning it.
    const auto &originalName = AppWindow::GetInstance()->originalProjectName_;
    if (!originalName.empty()) {
      PersistencyService::GetInstance()->ClearAutosave(originalName.c_str());
    }

    // first clear out any existing "unnamed" project
    PersistencyService::GetInstance()->PurgeUnnamedProject();

    ViewEvent ve(VET_NEW_PROJECT);
    ((ProjectView &)v).SetChanged();
    ((ProjectView &)v).NotifyObservers(&ve);
  }
};

static void BootselCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    System *sys = System::GetInstance();
    sys->SystemBootloader();
  }
};

static void SaveAsOverwriteCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_CANCEL) {
    return;
  }

  ProjectView &projectView = (ProjectView &)v;
  if (!SaveAsProject(projectView)) {
    MessageBox *mb = MessageBox::Create(projectView, "Failed to save project",
                                        MBBF_OK | MBBF_CANCEL);
    projectView.DoModal(mb);
  }
}

static void PurgeCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    ((ProjectView &)v).OnPurge();
  }
};

static void PurgeInstrumentsCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    ((ProjectView &)v).OnPurgeInstruments();
  }
};

ProjectView::ProjectView(GUIWindow &w, ViewData *data) : FieldView(w, data) {

  lastClock_ = 0;
  lastTick_ = 0;

  project_ = data->project_;

  GUIPoint position = GetAnchor();

  Variable *v = project_->FindVariable(FourCC::VarTempo);
  tempoField_.emplace_back(FourCC::ActionTempoChanged, position, *v,
                           "Tempo: %d [%2.2X]  ", MIN_TEMPO, MAX_TEMPO, 1, 10);
  fieldList_.insert(fieldList_.end(), &(*tempoField_.rbegin()));
  (*tempoField_.rbegin()).AddObserver(*this);

  v = project_->FindVariable(FourCC::VarTranspose);
  position._y += 1;
  intVarField_.emplace_back(position, *v, "Transpose: %3.2d", -48, 48, 0x1,
                            0xC);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  v = project_->FindVariable(FourCC::VarScale);
  // if scale name is not found, set the default chromatic scale
  if (v->GetInt() < 0) {
    v->SetInt(0);
  }
  position._y += 1;
  intVarField_.emplace_back(position, *v, "Scale: %s", 0, numScales - 1, 1, 10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  // Add Scale Root field
  position._y += 1;
  v = project_->FindVariable(FourCC::VarScaleRoot);
  intVarField_.emplace_back(position, *v, "Scale Root: %s", 0, 11, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 2;
  actionField_.emplace_back("Sample Pool", FourCC::ActionImport, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._y += 1;
  actionField_.emplace_back("Remove Unused Samples", FourCC::ActionPurge,
                            position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._y += 1;
  actionField_.emplace_back("Remove Unused Instruments",
                            FourCC::ActionPurgeInstrument, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._y += 1;
  actionField_.emplace_back("Recording", FourCC::ActionShowRecordView,
                            position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._y += 2;

  // save existing fields horizontal alignment
  int xalign = position._x;

  v = project_->FindVariable(FourCC::VarProjectName);
  auto label =
      etl::make_string_with_capacity<MAX_UITEXTFIELD_LABEL_LENGTH>("Project: ");
  auto defaultName = etl::make_string_with_capacity<MAX_PROJECT_NAME_LENGTH>(
      UNNAMED_PROJECT_NAME);
  textField_.emplace_back(*v, position, label, FourCC::ActionProjectRename,
                          defaultName, true);
  nameField_ = &(*textField_.rbegin());

  nameField_->AddObserver(*this);
  fieldList_.insert(fieldList_.end(), nameField_);

  position._y += 1;
  actionField_.emplace_back("Browse", FourCC::ActionBrowse, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._x += 7;
  actionField_.emplace_back("Save", FourCC::ActionSave, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._x += 5;
  actionField_.emplace_back("New", FourCC::ActionNewProject, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  position._x += 4;
  actionField_.emplace_back("Random", FourCC::ActionRandomName, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);
  position._x = xalign;

  // Add rendering action fields
  position._y += 2;

  // Add a static field as a label for the render actions
  renderLabelPos_ = position;
  staticField_.emplace_back(position, "Render:");
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  // Position the Mixdown action field on the line below the label
  position._y += 1;
  actionField_.emplace_back("Mixdown", FourCC::ActionRenderMixdown, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  mixdownField_ = &(*actionField_.rbegin());
  (*actionField_.rbegin()).AddObserver(*this);

  position._x += 8;
  actionField_.emplace_back("Stems", FourCC::ActionRenderStems, position);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  stemsField_ = &(*actionField_.rbegin());
  (*actionField_.rbegin()).AddObserver(*this);
  position._x = xalign;

  // MIDI input routing and default programs use the remaining six rows. MIDI
  // channels are ordered left-to-right, then top-to-bottom.
  position._y += 1;
  staticField_.emplace_back(position, "MIDI CHANNEL MAP", CD_HILITE2);
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._y += 1;
  for (int songChannel = 0; songChannel < SONG_CHANNEL_COUNT; songChannel++) {
    midiMapField_.emplace_back(
        position, project_->song_.midiInputChannels_[songChannel],
        MIDI_INPUT_CHANNEL_NONE, MIDI_INPUT_CHANNEL_COUNT,
        MIDI_INPUT_CHANNEL_NONE);
    fieldList_.insert(fieldList_.end(), &(*midiMapField_.rbegin()));
    position._x += 3;
  }

  position._x = xalign;
  position._y += 2;
  staticField_.emplace_back(position, "MIDI DEFAULT PROGRAM", CD_HILITE2);
  staticField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 17);
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._y += 1;
  for (int midiChannel = 0; midiChannel < MIDI_INPUT_CHANNEL_COUNT;
       midiChannel++) {
    if (midiChannel == SONG_CHANNEL_COUNT) {
      position._x = xalign;
      position._y += 1;
    }
    midiDefaultProgramField_.emplace_back(
        position, project_->song_.midiDefaultPrograms_[midiChannel],
        MIDI_INPUT_MIN_PROGRAM, MIDI_INPUT_MAX_PROGRAM,
        MIDI_INPUT_DEFAULT_PROGRAM);
    midiDefaultProgramField_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4,
                                                  17);
    fieldList_.insert(fieldList_.end(), &(*midiDefaultProgramField_.rbegin()));
    position._x += 3;
  }
}

ProjectView::~ProjectView() {}

void ProjectView::ProcessButtonMask(unsigned short mask, bool pressed) {

  if (!pressed)
    return;

  int8_t editedSongChannel = -1;
  uint8_t previousMidiMapValue = MIDI_INPUT_CHANNEL_NONE;
  int8_t editedMidiChannel = -1;
  uint8_t previousDefaultProgram = MIDI_INPUT_DEFAULT_PROGRAM;
  UIField *focusedField = GetFocus();
  for (size_t i = 0; i < midiMapField_.size(); i++) {
    if (focusedField == &midiMapField_[i]) {
      editedSongChannel = static_cast<int8_t>(i);
      previousMidiMapValue = midiMapField_[i].GetValue();
      break;
    }
  }
  for (size_t i = 0; i < midiDefaultProgramField_.size(); i++) {
    if (focusedField == &midiDefaultProgramField_[i]) {
      editedMidiChannel = static_cast<int8_t>(i);
      previousDefaultProgram = midiDefaultProgramField_[i].GetValue();
      break;
    }
  }

  FieldView::ProcessButtonMask(mask, pressed);

  if (editedSongChannel >= 0 &&
      previousMidiMapValue !=
          midiMapField_[static_cast<size_t>(editedSongChannel)].GetValue()) {
    Player::GetInstance()->OnMidiInputAssignmentChanged(
        static_cast<uint8_t>(editedSongChannel));
  }

  if (editedMidiChannel >= 0 &&
      previousDefaultProgram !=
          midiDefaultProgramField_[static_cast<size_t>(editedMidiChannel)]
              .GetValue()) {
    // Match a received Program Change for future notes while leaving the
    // edited project value in place as this channel's new default.
    MidiService *midiService = MidiService::GetInstance();
    if (midiService != nullptr) {
      midiService->SetInputProgramForChannel(
          editedMidiChannel,
          project_->song_.GetMidiDefaultInstrumentIndex(editedMidiChannel));
    }
  }

  if (mask & EPBM_EDIT) {
    if (mask & EPBM_PLAY) {
      // recording screen
      if (!Player::GetInstance()->IsRunning()) {
        switchToRecordView();
      }
    }
  } else if (mask & EPBM_NAV) {
    if (mask & EPBM_DOWN || mask & EPBM_UP) {
      if (!CanExit()) {
        return;
      }
    }

    if (mask & EPBM_DOWN) {
      ViewType vt = VT_SONG;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }
    if (mask & EPBM_UP) {
      ViewType vt = VT_DEVICE;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }
  } else if (mask & EPBM_PLAY) {
    Player *player = Player::GetInstance();
    player->OnStartButton(PM_SONG, viewData_->songX_, false, viewData_->songX_);
  }
};

void ProjectView::Reset() {
  lastClock_ = 0;
  lastTick_ = 0;
  saveAsFlag_ = false;
  nameField_->SetVariable(*project_->FindVariable(FourCC::VarProjectName));
  oldProjName_ = getProjectName();
}

void ProjectView::DrawView() {

  Clear();

  GUITextProperties props;
  props.region_ = TEXT_REGION_BODY;

  // Draw title
  Variable *v = viewData_->project_->FindVariable(FourCC::VarProjectName);
  etl::string<MAX_PROJECT_NAME_LENGTH> projectName = v->GetString();
  etl::string<23> title("Project ");
  title += projectName;
  setTitle(title);

  GUIRect selectionRect;
  bool hasSelection =
      static_cast<AppWindow &>(w_).GetSongSelectionRect(selectionRect);
  // Always remove Mixdown/Stems first then we add then back below only if
  // there is a active selection on the Song screen
  fieldList_.remove(mixdownField_);
  fieldList_.remove(stemsField_);
  if (!hasSelection) {
    // No selection: show "No Selection" on Render line
    if (GetFocus() == mixdownField_ || GetFocus() == stemsField_) {
      ClearFocus();
    }
    FieldView::Redraw();
    GUIPoint noSelPos = renderLabelPos_;
    noSelPos._x += 8;
    SetColor(CD_NORMAL);
    w_.DrawString("[No Selection]", noSelPos, props);
  } else {
    fieldList_.push_back(mixdownField_);
    fieldList_.push_back(stemsField_);
    FieldView::Redraw();
    DrawRenderSelectionSummary(selectionRect);
  }
  drawMap();
};

void ProjectView::DrawRenderSelectionSummary(const GUIRect &selectionRect) {
  const int32_t startRow = static_cast<int32_t>(selectionRect.Top());
  const int32_t endRow = static_cast<int32_t>(selectionRect.Bottom());
  const int32_t left = static_cast<int32_t>(selectionRect.Left());
  const int32_t right = static_cast<int32_t>(selectionRect.Right());

  etl::string<10> rowText("Row[");
  etl::format_spec rowFormat;
  rowFormat.hex().upper_case(true).width(2).fill('0');
  etl::to_string(startRow, rowText, rowFormat, true);
  rowText += '-';
  etl::to_string(endRow, rowText, rowFormat, true);
  rowText += ']';

  const uint8_t channelCount = SONG_CHANNEL_COUNT;
  const int32_t lastChannel = channelCount > 0 ? channelCount - 1 : 0;
  const int32_t startChannel = std::max(int32_t{0}, left);
  const int32_t endChannel = std::min(right, lastChannel);
  etl::string<7> channelText("Ch[");
  etl::to_string(startChannel + 1, channelText, true);
  channelText += '-';
  etl::to_string(endChannel + 1, channelText, true);
  channelText += ']';

  SetColor(CD_NORMAL);
  GUITextProperties props;
  props.region_ = TEXT_REGION_BODY;

  GUIPoint renderSelectionInfoPos = renderLabelPos_;
  renderSelectionInfoPos._x += 8;
  w_.DrawString(rowText.c_str(), renderSelectionInfoPos, props);

  renderSelectionInfoPos._x += 11;
  w_.DrawString(channelText.c_str(), renderSelectionInfoPos, props);
}

void ProjectView::Update(Observable &, I_ObservableData *data) {

  if (!hasFocus_) {
    return;
  }

  uintptr_t fourcc = (uintptr_t)data;

  UIField *focus = GetFocus();
  if (fourcc != FourCC::ActionTempoChanged) {
    focus->ClearFocus();
    focus->Draw(w_);
    w_.Flush();
    focus->SetFocus();
  } else {
    focus = &tempoField_[0];
  }
  Player *player = Player::GetInstance();

  switch (fourcc) {
  case FourCC::ActionPurge: {
    MessageBox *mb =
        MessageBox::Create(*this, "Remove unused samples?", MBBF_YES | MBBF_NO);
    DoModal(mb, ModalViewCallback::create<&PurgeCallback>());
    break;
  }
  case FourCC::ActionPurgeInstrument: {
    MessageBox *mb = MessageBox::Create(*this, "Remove unused instruments?",
                                        MBBF_YES | MBBF_NO);
    DoModal(mb, ModalViewCallback::create<&PurgeInstrumentsCallback>());
    break;
  }
  case FourCC::ActionRandomName: {
    char name[10];
    System *sys = System::GetInstance();
    uint32_t randNum = sys->GetRandomNumber();
    getNamesByIndex(name, randNum, 10);
    printf("random:%s", name);
    project_->SetProjectName(name);
    saveAsFlag_ = true;
    break;
  }
  case FourCC::ActionSave: {
    PersistencyService *persist = PersistencyService::GetInstance();
    char projName[MAX_PROJECT_NAME_LENGTH + 1];
    project_->GetProjectName(projName);

    if (saveAsFlag_) {
      // first need to check if project with this name already exists
      if (persist->Exists(projName)) {
        Trace::Error("project already exists ask user to confirm overwrite");
        MessageBox *mb = MessageBox::Create(
            *this, "Overwrite EXISTING project?", MBBF_OK | MBBF_CANCEL);
        DoModal(mb, ModalViewCallback::create<&SaveAsOverwriteCallback>());
        return;
      }
      if (!SaveAsProject(*this)) {
        MessageBox *mb =
            MessageBox::Create(*this, "Error saving Project", MBBF_OK);
        DoModal(mb);
      }
      return;
    } else {
      if (persist->Save(projName, oldProjName_.c_str(), saveAsFlag_) !=
          PERSIST_SAVED) {
        Trace::Error("failed to save project state");
        MessageBox *mb =
            MessageBox::Create(*this, "Error saving Project", MBBF_OK);
        DoModal(mb);
        return;
      }
    }
    // all good so now persist the new project name in project state
    persist->SaveProjectState(projName);
    break;
  }
  case FourCC::ActionProjectRename:
    Trace::Log("PROJECTVIEW", "Project renamed! prev name:%s",
               nameField_->GetString().c_str());
    saveAsFlag_ = true;
    break;
  case FourCC::ActionBrowse: {
    if (CanExit()) {
      ViewType vt = VT_SELECTPROJECT;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }
    break;
  }
  case FourCC::ActionNewProject: {
    MessageBox *mb =
        MessageBox::Create(*this, "Create a new project and",
                           "   lose all changes?", MBBF_YES | MBBF_NO);
    DoModal(mb, ModalViewCallback::create<&CreateNewProjectCallback>());
    break;
  }
  case FourCC::ActionBootSelect: {
    if (!player->IsRunning()) {
      MessageBox *mb = MessageBox::Create(*this, "Reboot and lose changes?",
                                          MBBF_YES | MBBF_NO);
      DoModal(mb, ModalViewCallback::create<&BootselCallback>());
    } else {
      MessageBox *mb = MessageBox::Create(*this, "Not while playing", MBBF_OK);
      DoModal(mb);
    }
    break;
  }

  case FourCC::ActionTempoChanged:
    break;
  case FourCC::ActionRenderMixdown:
    if (!player->IsRunning()) {
      SelectionResampleRequest request;
      if (!static_cast<AppWindow &>(w_).BuildSongSelectionRenderRequest(
              request, SelectionOutputIntent::Mixdown)) {
        const char *error =
            SelectionResampleSupport::GetProjectSamplePathError();
        MessageBox *mb = MessageBox::Create(
            *this, "      Render failed",
            error[0] != '\0' ? error : "Select rows in Song view first",
            MBBF_OK);
        DoModal(mb);
        break;
      }
      SelectionResampleSupport::StartSelectionResample(*this, request);
    }
    break;
  case FourCC::ActionRenderStems:
    if (!player->IsRunning()) {
      SelectionResampleRequest request;
      if (!static_cast<AppWindow &>(w_).BuildSongSelectionRenderRequest(
              request, SelectionOutputIntent::Stems)) {
        const char *error =
            SelectionResampleSupport::GetProjectSamplePathError();
        MessageBox *mb = MessageBox::Create(
            *this, "      Render failed",
            error[0] != '\0' ? error : "Select rows in Song view first",
            MBBF_OK);
        DoModal(mb);
        break;
      }
      SelectionResampleSupport::StartSelectionResample(*this, request);
    }
    break;
  case FourCC::ActionShowRecordView:
    switchToRecordView();
    break;
  case FourCC::ActionImport: {
    // Switch to the ImportView **BUT** to show the Project Pool by default
    // First check if the samplelib exists
    bool samplelibExists = FileSystem::GetInstance()->exists(SAMPLES_LIB_DIR);

    if (!samplelibExists) {
      MessageBox *mb =
          MessageBox::Create(*this, "Can't access the samplelib", MBBF_OK);
      DoModal(mb);
    } else {
      ImportView::SetSourceViewType(VT_PROJECT);
      // Set to show project pool dir in ImportView
      viewData_->isShowingSampleEditorProjectPool = true;

      // Go to import sample
      ViewType vt = VT_IMPORT;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }
    break;
  }
  default:
    NInvalid;
    break;
  };
  focus->Draw(w_);
  isDirty_ = true;
};

void ProjectView::OnPurge() { project_->PurgeSamples(); };

void ProjectView::OnPurgeInstruments() { project_->PurgeInstruments(); };

void ProjectView::OnQuit() {
  ViewEvent ve(VET_QUIT_APP);
  SetChanged();
  NotifyObservers(&ve);
};

void ProjectView::OnFocus() {
  // only store current project name for use in a "save as" operation if it's
  // not already been modified by the user pending a save which is indicated
  // by the saveAsFlag_ flag being set
  if (!saveAsFlag_) {
    oldProjName_ = getProjectName();
  }
}

bool ProjectView::CanExit() {
  if (saveAsFlag_) {
    MessageBox *mb =
        MessageBox::Create(*this, "Save project rename first", MBBF_OK);
    DoModal(mb);
    return false;
  }
  return true;
};
