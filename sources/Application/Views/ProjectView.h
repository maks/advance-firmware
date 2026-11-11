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

#ifndef _PROJECT_VIEW_H_
#define _PROJECT_VIEW_H_

#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UITempoField.h"
#include "BaseClasses/UITextField.h"
#include "FieldView.h"
#include "Foundation/Observable.h"
#include "UIFramework/BasicDatas/GUIPoint.h"
#include "UIFramework/BasicDatas/GUIRect.h"
#include "ViewData.h"
#include <stdint.h>

template <uint8_t MaxLength> class UITextField;

class ProjectMidiValueField : public UIField {
public:
  ProjectMidiValueField(const GUIPoint &position, uint8_t &value,
                        uint8_t minimum, uint8_t maximum, uint8_t defaultValue);

  void Draw(GUIWindow &w, int offset = 0) override;
  void ProcessArrow(unsigned short mask) override;
  void ProcessClear() override;
  void OnClick() override {}
  uint8_t GetValue() const;

private:
  uint8_t *value_;
  uint8_t minimum_;
  uint8_t maximum_;
  uint8_t defaultValue_;
};

class ProjectView : public FieldView, public I_Observer {
public:
  // The normal body starts below the two small-font information rows.
  GUIPoint GetAnchor() override { return GUIPoint(5, 0); }

  ProjectView(GUIWindow &w, ViewData *data);
  virtual ~ProjectView();
  void Reset();

  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
  virtual void OnFocus();

  etl::string<MAX_PROJECT_NAME_LENGTH> getProjectName() {
    return etl::string<MAX_PROJECT_NAME_LENGTH>(
        nameField_->GetString().c_str());
  };

  etl::string<MAX_PROJECT_NAME_LENGTH> getOldProjectName() {
    return oldProjName_;
  };
  void clearSaveAsFlag() {
    saveAsFlag_ = false;
    oldProjName_ = getProjectName();
  };

  // Observer for action callback

  void Update(Observable &, I_ObservableData *);

  void OnPurgeInstruments();
  void OnPurge();
  void OnQuit();

private:
  Project *project_;
  // Debug
  unsigned long lastTick_;
  unsigned long lastClock_;

  // Statically allocated field vectors
  etl::vector<UITempoField, 1> tempoField_;
  etl::vector<UIIntVarField, 4> intVarField_;
  etl::vector<UIActionField, 10> actionField_;
  etl::vector<UIStaticField, 3> staticField_;
  etl::vector<UITextField<MAX_PROJECT_NAME_LENGTH>, 1> textField_;
  etl::vector<ProjectMidiValueField, SONG_CHANNEL_COUNT> midiMapField_;
  etl::vector<ProjectMidiValueField, MIDI_INPUT_CHANNEL_COUNT>
      midiDefaultProgramField_;

  // References to specific fields that need direct access
  UITextField<MAX_PROJECT_NAME_LENGTH> *nameField_;
  UIActionField *mixdownField_ = nullptr;
  UIActionField *stemsField_ = nullptr;
  GUIPoint renderLabelPos_;
  bool saveAsFlag_ = false;
  etl::string<MAX_PROJECT_NAME_LENGTH> oldProjName_;

  void DrawRenderSelectionSummary(const GUIRect &selectionRect);
  bool CanExit();
};
#endif
