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

#ifndef _TABLE_VIEW_H_
#define _TABLE_VIEW_H_

#include "Application/Model/Table.h"
#include "BaseClasses/CommandHelpState.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "ScreenView.h"
#include "ViewData.h"

class TableView : public ScreenView {
public:
  // The normal body starts below the two small-font information rows.
  GUIPoint GetAnchor() override { return GUIPoint(5, 0); }

  TableView(GUIWindow &w, ViewData *viewData);
  ~TableView();
  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();
  virtual void AnimationUpdate();

protected:
  void processNormalButtonMask(unsigned short mask);
  void processSelectionButtonMask(unsigned short mask);

  void cutPosition();
  void pasteLast();
  void copySelection();
  void cutSelection();
  void pasteClipboard();

  void fillClipboardData();
  void extendSelection();

  void updateCursor(int dx, int dy);
  void updateCursorValue(int offset, int xOffset = 0, int yOffset = 0);
  void updateSelectionValue(int offset);
  void setTextProps(GUITextProperties &props, int row, int col, bool restore);
  void warpToNeighbour(int dir);

  GUIRect getSelectionRect();

private:
  CommandHelpState commandHelp_;
  enum class TableClipboardColumnType : uint8_t {
    Command,
    Parameter,
    Invalid,
  };

  int8_t row_;
  int8_t col_;
  uchar lastVol_;
  uchar lastTick_;
  uchar lastTsp_;
  int lastCmd_;
  int lastParam_;

  Variable cmdEdit_;
  GUIPoint cmdEditPos_;
  UIBigHexVarField cmdEditField_;

  struct clipboard {
    bool active_;
    int col_;
    int row_;
    int width_;
    int height_;
    uchar cmd1_[16];
    ushort param1_[16];
    uchar cmd2_[16];
    ushort param2_[16];
    uchar cmd3_[16];
    ushort param3_[16];
    TableClipboardColumnType columnTypes_[6];
  } clipboard_;

  int saveCol_;
  int saveRow_;
  bool selectionCopyPending_ = false;

  // Flags to track which UI elements need updating
  // These prevent core1 from directly updating the UI
  bool needsUIUpdate_ =
      false; // Single flag for all UI updates (notes, VU meter, positions)

  // Keep these for backward compatibility
  bool needsPlayPositionUpdate_ = false;
  bool needsNotesUpdate_ = false;

  uchar lastPosition_[3];
};

#endif
