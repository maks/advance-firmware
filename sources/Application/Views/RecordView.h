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

#ifndef _RECORD_VIEW_H_
#define _RECORD_VIEW_H_

#include "Application/Model/Config.h"
#include "Application/Views/BaseClasses/View.h"
#include "BaseClasses/UIIntVarField.h"
#include "FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"

class RecordView : public FieldView, public I_Observer {
public:
  TextLayout GetTextLayout() const override {
    static constexpr TextRegion regions[] = {
        {8, 4, 704, 24, TEXT_FONT_SMALL},
        {8, 30, 704, 690, TEXT_FONT_NORMAL},
    };
    return {regions, sizeof(regions) / sizeof(regions[0])};
  }

  RecordView(GUIWindow &w, ViewData *data);
  virtual ~RecordView();
  void Reset();

  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
  virtual void OnFocus();
  void OnFocusLost() override;

  // Observer for field changes
  void Update(Observable &, I_ObservableData *);

  void AnimationUpdate() override;

  // Static method to set which view will open the RecordView
  static void SetSourceViewType(ViewType vt);

  // Track which view opened the RecordView (defaults to song view)
  static ViewType sourceViewType_;

protected:
private:
  // UI fields
  etl::vector<UIIntVarField, 3> intVarField_;

  // Recording state
  bool uiRecordingActive_;
  bool uiSavingActive_;
  bool autoSwitchPending_;
  uint32_t recordingStartTime_;
  uint32_t recordingDuration_;

  // Helper methods
  void record();
  void stop();
  void stopAndSwitchToEditor();
  void updateTimeDisplay();
  bool generateFullPath(const char *filename,
                        etl::string<PFILENAME_SIZE> *fullpath);

  // Time display helpers
  void formatTime(uint32_t milliseconds, char *buffer, size_t bufferSize);

  void updateRecordingSource();
};

#endif
