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

#include "TableView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Views/BaseClasses/SelectionButtonBehavior.h"
#include "Application/Views/SampleEditorView.h"
#include "Externals/etl/include/etl/to_string.h"
#include "System/Console/Trace.h"
#include "ViewData.h"
#include <nanoprintf.h>

TableView::TableView(GUIWindow &w, ViewData *viewData)
    : ScreenView(w, viewData), cmdEdit_(FourCC::ActionEdit, 0),
      cmdEditPos_(0, 10),
      cmdEditField_(cmdEditPos_, cmdEdit_, 4, "%4.4X", 0, 0xFFFF, 16, true) {
  row_ = 0;
  col_ = 0;

  lastVol_ = 0;
  lastTick_ = 0;
  lastTsp_ = 0;
  lastCmd_ = TrackerCommand::InstrumentCommandNone;
  lastParam_ = 0;

  clipboard_.active_ = false;
  clipboard_.width_ = 0;
  clipboard_.height_ = 0;
}

TableView::~TableView() {}

void TableView::Reset() {
  row_ = 0;
  col_ = 0;
  lastVol_ = 0;
  lastTick_ = 0;
  lastTsp_ = 0;
  lastCmd_ = TrackerCommand::InstrumentCommandNone;
  lastParam_ = 0;
  cmdEdit_.SetInt(0, false);

  clipboard_.active_ = false;
  clipboard_.width_ = 0;
  clipboard_.height_ = 0;
  clipboard_.col_ = 0;
  clipboard_.row_ = 0;
  for (int i = 0; i < 16; i++) {
    clipboard_.cmd1_[i] = 0;
    clipboard_.param1_[i] = 0;
    clipboard_.cmd2_[i] = 0;
    clipboard_.param2_[i] = 0;
    clipboard_.cmd3_[i] = 0;
    clipboard_.param3_[i] = 0;
  }
  for (int i = 0; i < 6; i++) {
    clipboard_.columnTypes_[i] = TableClipboardColumnType::Invalid;
  }

  saveCol_ = 0;
  saveRow_ = 0;
  needsUIUpdate_ = false;
  needsPlayPositionUpdate_ = false;
  needsNotesUpdate_ = false;
  lastPosition_[0] = 0;
  lastPosition_[1] = 0;
  lastPosition_[2] = 0;
}

void TableView::OnFocus() {
  clipboard_.active_ = false;
  viewMode_ = VM_NORMAL;
  lastPosition_[0] = lastPosition_[1] = lastPosition_[2] = 0xFF;
  updateCursor(0, 0);
};

void TableView::cutPosition() {

  clipboard_.active_ = true;
  clipboard_.row_ = row_;
  clipboard_.col_ = col_;
  saveRow_ = row_;
  saveCol_ = col_;

  if ((col_ == 0) || (col_ == 2) || (col_ == 4))
    col_ += 1; // This way, A+B on note cuts
               // the instruments too and parameters get cut with commands
  cutSelection();
};

GUIRect TableView::getSelectionRect() {
  GUIRect r(clipboard_.col_, clipboard_.row_, col_, row_);
  r.Normalize();
  return r;
};

void TableView::fillClipboardData() {

  auto getColumnType = [](int column) {
    switch (column) {
    case 0:
    case 2:
    case 4:
      return TableClipboardColumnType::Command;
    case 1:
    case 3:
    case 5:
      return TableClipboardColumnType::Parameter;
    default:
      return TableClipboardColumnType::Invalid;
    }
  };

  // Get Current normalized selection rect

  GUIRect selRect = getSelectionRect();

  // Get size & store in clipboard

  clipboard_.width_ = selRect.Width() + 1;
  clipboard_.height_ = selRect.Height() + 1;
  clipboard_.row_ = selRect.Top();
  clipboard_.col_ = selRect.Left();
  for (int i = 0; i < 6; i++) {
    clipboard_.columnTypes_[i] = TableClipboardColumnType::Invalid;
  }
  for (int i = 0; i < clipboard_.width_; i++) {
    clipboard_.columnTypes_[i] = getColumnType(clipboard_.col_ + i);
  }

  // Copy the data

  Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

  uchar *src1 = (unsigned char *)table.cmd1_;
  uchar *dst1 = clipboard_.cmd1_;
  ushort *src2 = table.param1_;
  ushort *dst2 = clipboard_.param1_;
  uchar *src3 = (unsigned char *)table.cmd2_;
  uchar *dst3 = clipboard_.cmd2_;
  ushort *src4 = table.param2_;
  ushort *dst4 = clipboard_.param2_;
  uchar *src5 = (unsigned char *)table.cmd3_;
  uchar *dst5 = clipboard_.cmd3_;
  ushort *src6 = table.param3_;
  ushort *dst6 = clipboard_.param3_;

  for (int i = 0; i < clipboard_.height_; i++) {
    dst1[i] = src1[clipboard_.row_ + i];
    dst2[i] = src2[clipboard_.row_ + i];
    dst3[i] = src3[clipboard_.row_ + i];
    dst4[i] = src4[clipboard_.row_ + i];
    dst5[i] = src5[clipboard_.row_ + i];
    dst6[i] = src6[clipboard_.row_ + i];
  };
  updateCursor(0, 0);
};

void TableView::extendSelection() {
  GUIRect rect = getSelectionRect();
  if (rect.Left() > 0 || rect.Right() < 6) {
    if (col_ < clipboard_.col_) {
      col_ = 0;
      clipboard_.col_ = 6;
    } else {
      col_ = 6;
      clipboard_.col_ = 0;
    }
    isDirty_ = true;
  } else {
    if (row_ < clipboard_.row_) {
      row_ = 0;
      clipboard_.row_ = 15;
    } else {
      clipboard_.row_ = 0;
      row_ = 15;
    }
    isDirty_ = true;
  }
}

void TableView::copySelection() {

  // Keep up with row,col of selection coz
  // fillClipboardData will trash it

  fillClipboardData();

  selectionCopyPending_ = false;
  clipboard_.active_ = false;
  viewMode_ = VM_NORMAL;
  row_ = saveRow_;
  col_ = saveCol_;

  isDirty_ = true;
};

void TableView::cutSelection() {

  // Keep up with row,col of selection coz
  // fillClipboardData will trash it

  fillClipboardData();
  selectionCopyPending_ = false;

  // Loop over selection col, row & clear data inside it

  Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);
  uchar *dst1 = (unsigned char *)table.cmd1_;
  ushort *dst2 = table.param1_;
  uchar *dst3 = (unsigned char *)table.cmd2_;
  ushort *dst4 = table.param2_;
  uchar *dst5 = (unsigned char *)table.cmd3_;
  ushort *dst6 = table.param3_;

  for (int i = 0; i < clipboard_.width_; i++) {
    for (int j = 0; j < clipboard_.height_; j++) {
      switch (i + clipboard_.col_) {
      case 0:
        dst1[j + clipboard_.row_] = TrackerCommand::InstrumentCommandNone;
        break;
      case 1:
        dst2[j + clipboard_.row_] = 0x0000;
        break;
      case 2:
        dst3[j + clipboard_.row_] = TrackerCommand::InstrumentCommandNone;
        break;
      case 3:
        dst4[j + clipboard_.row_] = 0x0000;
        break;
      case 4:
        dst5[j + clipboard_.row_] = TrackerCommand::InstrumentCommandNone;
        break;
      case 5:
        dst6[j + clipboard_.row_] = 0x0000;
        break;
      }
    }
  }

  // Clear selection, end selection process & reposition cursor

  clipboard_.active_ = false;
  viewMode_ = VM_NORMAL;
  row_ = saveRow_;
  col_ = saveCol_;
  updateCursor(0, 0);
  isDirty_ = true;
};

/******************************************************
 pasteClipboard:
        copies data in the clipboard to the current step
 ******************************************************/

void TableView::pasteClipboard() {
  auto getColumnType = [](int column) {
    switch (column) {
    case 0:
    case 2:
    case 4:
      return TableClipboardColumnType::Command;
    case 1:
    case 3:
    case 5:
      return TableClipboardColumnType::Parameter;
    default:
      return TableClipboardColumnType::Invalid;
    }
  };

  // Get number of row to paste

  int width = clipboard_.width_;
  int height = clipboard_.height_;
  if (col_ + width > 6) {
    width = 6 - col_;
  }
  /*    if (row_+height>16) {
          height=16-row_ ;
      }
    */
  Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

  uchar *dst1 = (unsigned char *)table.cmd1_;
  uchar *src1 = clipboard_.cmd1_;
  ushort *dst2 = table.param1_;
  ushort *src2 = clipboard_.param1_;
  uchar *dst3 = (unsigned char *)table.cmd2_;
  uchar *src3 = clipboard_.cmd2_;
  ushort *dst4 = table.param2_;
  ushort *src4 = clipboard_.param2_;
  uchar *dst5 = (unsigned char *)table.cmd3_;
  uchar *src5 = clipboard_.cmd3_;
  ushort *dst6 = table.param3_;
  ushort *src6 = clipboard_.param3_;

  auto readClipboardValue = [&](int sourceColumn, int sourceRow) -> ushort {
    switch (sourceColumn) {
    case 0:
      return src1[sourceRow];
    case 1:
      return src2[sourceRow];
    case 2:
      return src3[sourceRow];
    case 3:
      return src4[sourceRow];
    case 4:
      return src5[sourceRow];
    case 5:
      return src6[sourceRow];
    default:
      return 0;
    }
  };

  auto writeDestinationValue = [&](int destinationColumn, int destinationRow,
                                   ushort value) {
    switch (destinationColumn) {
    case 0:
      dst1[destinationRow] = static_cast<uchar>(value);
      break;
    case 1:
      dst2[destinationRow] = value;
      break;
    case 2:
      dst3[destinationRow] = static_cast<uchar>(value);
      break;
    case 3:
      dst4[destinationRow] = value;
      break;
    case 4:
      dst5[destinationRow] = static_cast<uchar>(value);
      break;
    case 5:
      dst6[destinationRow] = value;
      break;
    }
  };

  for (int i = 0; i < width; i++) {
    int sourceColumn = i + clipboard_.col_;
    int destinationColumn = i + col_;
    if (clipboard_.columnTypes_[i] != getColumnType(destinationColumn)) {
      continue;
    }
    for (int j = 0; j < height; j++) {
      ushort value = readClipboardValue(sourceColumn, j);
      writeDestinationValue(destinationColumn, (j + row_) % 16, value);
    }
  }
  int offset = (row_ + height) % 16 - row_;
  updateCursor(0x00, offset);
  isDirty_ = true;
};

void TableView::updateCursor(int dx, int dy) {
  col_ += dx;
  row_ += dy;
  if (col_ > 5) {
    col_ = 5;
  }
  if (col_ < 0) {
    col_ = 0;
  }
  if (row_ > 15) {
    row_ = 15;
  }
  if (row_ < 0) {
    row_ = 0;
  }
  Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

  GUIPoint anchor = GetAnchor();
  GUIPoint p(anchor);
  switch (col_) {
  case 1:
    p._x += 4;
    p._y += row_;
    cmdEditField_.SetPosition(p);
    cmdEdit_.SetInt(*(table.param1_ + row_));
    break;
  case 3:
    p._x += 13;
    p._y += row_;
    cmdEditField_.SetPosition(p);
    cmdEdit_.SetInt(*(table.param2_ + row_));
    break;
  case 5:
    p._x += 22;
    p._y += row_;
    cmdEditField_.SetPosition(p);
    cmdEdit_.SetInt(*(table.param3_ + row_));
    break;
  };

  isDirty_ = true;
};

void TableView::warpToNeighbour(int dir) {

  int current = viewData_->currentTable_ + dir;

  if (current >= TABLE_COUNT) {
    current -= TABLE_COUNT;
  }
  if (current < 0) {
    current += TABLE_COUNT;
  }
  viewData_->currentTable_ = current;
  updateCursor(0, 0);
  isDirty_ = true;
}

void TableView::updateCursorValue(int offset, int xOffset, int yOffset) {

  unsigned char *c = 0;
  unsigned char limit = 0;
  bool wrap = false;
  TrackerCommand *cc;

  Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

  switch (col_ + xOffset) {
  case 0: {
    cc = table.cmd1_ + row_ + yOffset;
    if ((viewMode_ == VM_SELECTION) &&
        (*cc == TrackerCommand::InstrumentCommandNone)) {
      break;
    }
    switch (offset) {
    case 0x01:
      *cc = CommandList::GetNext(*cc, CommandContext::Table);
      break;
    case 0x10:
      *cc = CommandList::GetNextAlpha(*cc, CommandContext::Table);
      break;
    case -0x01:
      *cc = CommandList::GetPrev(*cc, CommandContext::Table);
      break;
    case -0x10:
      *cc = CommandList::GetPrevAlpha(*cc, CommandContext::Table);
      break;
    }
    lastCmd_ = *cc;
    break;
  }
  case 1: {
    TrackerCommand currentCmd = *(table.cmd1_ + row_ + yOffset);
    if ((viewMode_ == VM_SELECTION) &&
        (currentCmd == TrackerCommand::InstrumentCommandNone)) {
      break;
    }
    cmdEdit_.SetInt(*(table.param1_ + row_ + yOffset));
    Trace::Log("CMD_EDIT_PROBE",
               "Table param edit col:%d row:%d y:%d offset:%d mode:%d "
               "before:%04x",
               col_ + xOffset, row_, yOffset, offset, viewMode_,
               cmdEdit_.GetInt());
    if (viewMode_ == VM_SELECTION) {
      if (offset == 0x10) {
        cmdEditField_.ApplyOffset(1);
      } else if (offset == -0x10) {
        cmdEditField_.ApplyOffset(-1);
      }
    } else {
      switch (offset) {
      case 0x01:
        cmdEditField_.ProcessArrow(EPBM_RIGHT);
        break;
      case 0x10:
        cmdEditField_.ProcessArrow(EPBM_UP);
        break;
      case -0x01:
        cmdEditField_.ProcessArrow(EPBM_LEFT);
        break;
      case -0x10:
        cmdEditField_.ProcessArrow(EPBM_DOWN);
        break;
      }
    }
    // Sanitize MIDI velocity values if needed
    ushort paramValue = cmdEdit_.GetInt();
    paramValue = CommandList::RangeLimitCommandParam(currentCmd, paramValue);
    cmdEdit_.SetInt(paramValue);
    *(table.param1_ + row_ + yOffset) = paramValue;
    lastParam_ = paramValue;
    break;
  }
  case 2: {
    cc = table.cmd2_ + row_ + yOffset;
    if ((viewMode_ == VM_SELECTION) &&
        (*cc == TrackerCommand::InstrumentCommandNone)) {
      break;
    }
    switch (offset) {
    case 0x01:
      *cc = CommandList::GetNext(*cc, CommandContext::Table);
      break;
    case 0x10:
      *cc = CommandList::GetNextAlpha(*cc, CommandContext::Table);
      break;
    case -0x01:
      *cc = CommandList::GetPrev(*cc, CommandContext::Table);
      break;
    case -0x10:
      *cc = CommandList::GetPrevAlpha(*cc, CommandContext::Table);
      break;
    }
    lastCmd_ = *cc;
    break;
  }
  case 3: {
    TrackerCommand currentCmd = *(table.cmd2_ + row_ + yOffset);
    if ((viewMode_ == VM_SELECTION) &&
        (currentCmd == TrackerCommand::InstrumentCommandNone)) {
      break;
    }
    cmdEdit_.SetInt(*(table.param2_ + row_ + yOffset));
    Trace::Log("CMD_EDIT_PROBE",
               "Table param edit col:%d row:%d y:%d offset:%d mode:%d "
               "before:%04x",
               col_ + xOffset, row_, yOffset, offset, viewMode_,
               cmdEdit_.GetInt());
    if (viewMode_ == VM_SELECTION) {
      if (offset == 0x10) {
        cmdEditField_.ApplyOffset(1);
      } else if (offset == -0x10) {
        cmdEditField_.ApplyOffset(-1);
      }
    } else {
      switch (offset) {
      case 0x01:
        cmdEditField_.ProcessArrow(EPBM_RIGHT);
        break;
      case 0x10:
        cmdEditField_.ProcessArrow(EPBM_UP);
        break;
      case -0x01:
        cmdEditField_.ProcessArrow(EPBM_LEFT);
        break;
      case -0x10:
        cmdEditField_.ProcessArrow(EPBM_DOWN);
        break;
      }
    }
    // Sanitize MIDI velocity values if needed
    ushort paramValue = cmdEdit_.GetInt();
    paramValue = CommandList::RangeLimitCommandParam(currentCmd, paramValue);
    cmdEdit_.SetInt(paramValue);
    *(table.param2_ + row_ + yOffset) = paramValue;
    lastParam_ = paramValue;
    break;
  }
  case 4: {
    cc = table.cmd3_ + row_ + yOffset;
    if ((viewMode_ == VM_SELECTION) &&
        (*cc == TrackerCommand::InstrumentCommandNone)) {
      break;
    }
    switch (offset) {
    case 0x01:
      *cc = CommandList::GetNext(*cc, CommandContext::Table);
      break;
    case 0x10:
      *cc = CommandList::GetNextAlpha(*cc, CommandContext::Table);
      break;
    case -0x01:
      *cc = CommandList::GetPrev(*cc, CommandContext::Table);
      break;
    case -0x10:
      *cc = CommandList::GetPrevAlpha(*cc, CommandContext::Table);
      break;
    }
    lastCmd_ = *cc;
    break;
  }
  case 5: {
    TrackerCommand currentCmd = *(table.cmd3_ + row_ + yOffset);
    if ((viewMode_ == VM_SELECTION) &&
        (currentCmd == TrackerCommand::InstrumentCommandNone)) {
      break;
    }
    cmdEdit_.SetInt(*(table.param3_ + row_ + yOffset));
    Trace::Log("CMD_EDIT_PROBE",
               "Table param edit col:%d row:%d y:%d offset:%d mode:%d "
               "before:%04x",
               col_ + xOffset, row_, yOffset, offset, viewMode_,
               cmdEdit_.GetInt());
    if (viewMode_ == VM_SELECTION) {
      if (offset == 0x10) {
        cmdEditField_.ApplyOffset(1);
      } else if (offset == -0x10) {
        cmdEditField_.ApplyOffset(-1);
      }
    } else {
      switch (offset) {
      case 0x01:
        cmdEditField_.ProcessArrow(EPBM_RIGHT);
        break;
      case 0x10:
        cmdEditField_.ProcessArrow(EPBM_UP);
        break;
      case -0x01:
        cmdEditField_.ProcessArrow(EPBM_LEFT);
        break;
      case -0x10:
        cmdEditField_.ProcessArrow(EPBM_DOWN);
        break;
      }
    }
    // Sanitize MIDI velocity values if needed
    ushort paramValue = cmdEdit_.GetInt();
    paramValue = CommandList::RangeLimitCommandParam(currentCmd, paramValue);
    cmdEdit_.SetInt(paramValue);
    *(table.param3_ + row_ + yOffset) = paramValue;
    lastParam_ = paramValue;
    break;
  }
  }
  if (c) {
    updateData(c, offset, limit, wrap);
    switch (col_ + xOffset) {
    case 0:
      lastVol_ = *c;
      break;
    case 1:
      lastTick_ = *c;
      break;
    case 2:
      lastTsp_ = *c;
      break;
    }
  }
  isDirty_ = true;
}

void TableView::updateSelectionValue(int offset) {
  saveRow_ = row_;
  saveCol_ = col_;

  GUIRect r = getSelectionRect();
  col_ = r.Left();
  row_ = r.Top();

  for (int i = 0; i <= r.Width(); i++) {
    for (int j = 0; j <= r.Height(); j++) {
      if (col_ + i < 6) {
        updateCursorValue(offset, i, j);
      }
    }
  }
  row_ = saveRow_;
  col_ = saveCol_;
}

void TableView::pasteLast() {
  uchar *c = 0;

  Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

  switch (col_) {
  case 0:
    c = (unsigned char *)table.cmd1_ + row_;
    if (*c == TrackerCommand::InstrumentCommandNone) {
      *c = lastCmd_;
      isDirty_ = true;
    } else {
      lastCmd_ = *c;
    }
    break;

  case 1:
    break;

  case 2:
    c = (unsigned char *)table.cmd2_ + row_;
    if (*c == TrackerCommand::InstrumentCommandNone) {
      *c = lastCmd_;
      isDirty_ = true;
    } else {
      lastCmd_ = *c;
    }
    break;

  case 3:
    break;

  case 4:
    c = (unsigned char *)table.cmd3_ + row_;
    if (*c == TrackerCommand::InstrumentCommandNone) {
      *c = lastCmd_;
      isDirty_ = true;
    } else {
      lastCmd_ = *c;
    }
    break;

  case 5:
    break;
  }
};

void TableView::ProcessButtonMask(unsigned short mask, bool pressed) {
  Trace::Log("BUTTON_PROBE",
             "Table ProcessButtonMask mask:%04x pressed:%d mode:%d col:%d "
             "row:%d",
             mask, pressed ? 1 : 0, viewMode_, col_, row_);

  if (!pressed) {
    if (viewMode_ == VM_SELECTION &&
        GetSelectionEditAction(mask, false, EPBM_EDIT, EPBM_ENTER,
                               selectionCopyPending_) ==
            SelectionEditAction::Copy) {
      copySelection();
    }
    return;
  }
  if (viewMode_ == VM_SELECTION) {
    if (!clipboard_.active_) {
      clipboard_.active_ = true;
      clipboard_.col_ = col_;
      clipboard_.row_ = row_;
      saveCol_ = col_;
      saveRow_ = row_;
    }
    processSelectionButtonMask(mask);
  } else {
    processNormalButtonMask(mask);
  };
}

void TableView::processNormalButtonMask(unsigned short mask) {

  Player *player = Player::GetInstance();

  if (mask & EPBM_EDIT) {
    if (mask & EPBM_LEFT)
      warpToNeighbour(-1);
    if (mask & EPBM_RIGHT)
      warpToNeighbour(+1);
    if (mask & EPBM_DOWN)
      warpToNeighbour(-16);
    if (mask & EPBM_UP)
      warpToNeighbour(16);
    if (mask & EPBM_ENTER)
      cutPosition();
    if (mask & EPBM_ALT)
      viewMode_ = VM_SELECTION;
    if (mask & EPBM_PLAY) {
      // recording screen
      if (!Player::GetInstance()->IsRunning()) {
        switchToRecordView();
      }
    }
  } else if (mask & EPBM_ENTER) {
    // ENTER modifier
    if (mask & EPBM_DOWN)
      updateCursorValue(-0x10);
    if (mask & EPBM_UP)
      updateCursorValue(0x10);
    if (mask & EPBM_LEFT)
      updateCursorValue(-0x01);
    if (mask & EPBM_RIGHT)
      updateCursorValue(0x01);
    if (mask == EPBM_ENTER)
      pasteLast();
    if (mask & EPBM_ALT)
      pasteClipboard();
  } else if (mask & EPBM_NAV) {
    // NAV Modifier
    if (mask & EPBM_UP) {
      ViewType vt = (viewType_ == VT_TABLE ? VT_PHRASE : VT_INSTRUMENT);
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }
    if (mask & EPBM_LEFT) {
      if (viewType_ == VT_TABLE2) {
        ViewType vt = VT_TABLE;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        SetChanged();
        NotifyObservers(&ve);
      }
    }
    if (mask & EPBM_RIGHT) {
      if (viewType_ == VT_TABLE) {
        ViewType vt = VT_TABLE2;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        SetChanged();
        NotifyObservers(&ve);
      }
    }
    if (mask & EPBM_PLAY) {
      player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                            viewData_->chainRow_);
    }

  } else {
    // No modifier
    if (mask & EPBM_DOWN)
      updateCursor(0, 1);
    if (mask & EPBM_UP)
      updateCursor(0, -1);
    if (mask & EPBM_LEFT)
      updateCursor(-1, 0);
    if (mask & EPBM_RIGHT)
      updateCursor(1, 0);
    if (mask & EPBM_PLAY) {
      player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                            viewData_->chainRow_);
    }
  }
}

void TableView::processSelectionButtonMask(unsigned short mask) {

  Player *player = Player::GetInstance();
  const SelectionEditAction editAction = GetSelectionEditAction(
      mask, true, EPBM_EDIT, EPBM_ENTER, selectionCopyPending_);
  if (editAction == SelectionEditAction::Cut) {
    cutSelection();
    return;
  }

  if (mask & EPBM_EDIT) {
    if (mask & EPBM_ALT) {
      extendSelection();
    } else if (mask != EPBM_EDIT) {
      copySelection();
    }
  } else {

    // A Modifer

    if (mask & EPBM_ENTER) {
      if (mask & EPBM_DOWN)
        updateSelectionValue(-0x10);
      if (mask & EPBM_UP)
        updateSelectionValue(0x10);
      if (mask & EPBM_LEFT)
        updateSelectionValue(-0x01);
      if (mask & EPBM_RIGHT)
        updateSelectionValue(0x01);
      if (mask & EPBM_ALT)
        cutSelection();
      //		if (mask&EPBM_R) switchSoloMode() ;
    } else {

      // R Modifier

      if (mask & EPBM_NAV) {
        if (mask & EPBM_UP) {
          ViewType vt = VT_PHRASE;
          ViewEvent ve(VET_SWITCH_VIEW, &vt);
          SetChanged();
          NotifyObservers(&ve);
        }
        if (mask & EPBM_PLAY) {
          player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                viewData_->chainRow_);
        }
        /*			if (mask&EPBM_L) unMuteAll() ;
         */
      } else {
        // L Modifier
        if (mask & EPBM_ALT) {

        } else {
          // No modifier

          if (mask & EPBM_DOWN)
            updateCursor(0, 1);
          if (mask & EPBM_UP)
            updateCursor(0, -1);
          if (mask & EPBM_LEFT)
            updateCursor(-1, 0);
          if (mask & EPBM_RIGHT)
            updateCursor(1, 0);
          if (mask & EPBM_PLAY) {
            player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                                  viewData_->chainRow_);
          }
        }
      }
    }
  }
}

void TableView::setTextProps(GUITextProperties &props, int row, int col,
                             bool restore) {

  bool invert = false;

  // Block selections are styled once after all ordinary fields are drawn.
  if (!clipboard_.active_) {
    if ((col_ == row) && (row_ == col)) {
      invert = true;
    }
  }

  if (invert) {
    if (restore) {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    } else {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    }
  }
}

void TableView::DrawView() {

  Clear();
  commandHelp_.Reset();

  GUITextProperties props;
  GUIPoint pos = GetTitlePosition();

  Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

  // Draw title (help text will overwrite it when cursor is on a command column)
  etl::string<23> title("Table ");
  etl::format_spec format;
  format.width(2).fill('0');
  etl::to_string(viewData_->currentTable_, title, format, true);
  setTitle(title);
  // drawTitlebar(TEXT_REGION_HEADER, props);

  // Compute song grid location

  GUIPoint anchor = GetAnchor();

  // Display row numbers
  SetColor(CD_HILITE1);
  char buffer[6];
  pos = anchor;
  pos._x -= 3;
  for (int j = 0; j < 16; j++) {
    ((j / ALT_ROW_NUMBER) % 2) ? SetColor(CD_ACCENT) : SetColor(CD_ACCENTALT);
    hex2char(j, buffer);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, buffer, props);
    pos._y++;
  }

  SetColor(CD_NORMAL);

  // Draw command 1

  pos = anchor;

  TrackerCommand *f = table.cmd1_;

  for (int j = 0; j < 16; j++) {
    TrackerCommand command = *f++;
    setTextProps(props, 0, j, false);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, command.c_str(), props);
    setTextProps(props, 0, j, true);
    pos._y++;
  }

  // Draw commands params 1

  pos = anchor;
  pos._x += 4;

  ushort *param = table.param1_;
  buffer[5] = 0;

  for (int j = 0; j < 16; j++) {
    ushort p = *param++;
    setTextProps(props, 1, j, false);
    hexshort2char(p, buffer);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, buffer, props);
    setTextProps(props, 1, j, true);
    pos._y++;
  }

  // Draw commands 2

  pos = anchor;
  pos._x += 9;

  f = table.cmd2_;

  for (int j = 0; j < 16; j++) {
    TrackerCommand command = *f++;
    setTextProps(props, 2, j, false);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, command.c_str(), props);
    setTextProps(props, 2, j, true);
    pos._y++;
  }

  // Draw commands params

  pos = anchor;
  pos._x += 13;

  param = table.param2_;
  buffer[5] = 0;

  for (int j = 0; j < 16; j++) {
    ushort p = *param++;
    setTextProps(props, 3, j, false);
    hexshort2char(p, buffer);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, buffer, props);
    setTextProps(props, 3, j, true);
    pos._y++;
  }

  // Draw command 3

  pos = anchor;
  pos._x += 18;

  f = table.cmd3_;

  for (int j = 0; j < 16; j++) {
    TrackerCommand command = *f++;
    setTextProps(props, 4, j, false);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, command.c_str(), props);
    setTextProps(props, 4, j, true);
    pos._y++;
  }

  // Draw commands params 3

  pos = anchor;
  pos._x += 22;

  param = table.param3_;
  buffer[5] = 0;

  for (int j = 0; j < 16; j++) {
    ushort p = *param++;
    setTextProps(props, 5, j, false);
    hexshort2char(p, buffer);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, buffer, props);
    setTextProps(props, 5, j, true);
    pos._y++;
  }

  if ((viewMode_ != VM_SELECTION) &&
      ((col_ == 1) || (col_ == 3) || (col_ == 5))) {
    cmdEditField_.SetFocus();
    cmdEditField_.Draw(w_);
  };

  if (clipboard_.active_) {
    // Logical fields have different widths; include only separators BETWEEN
    // selected fields, never padding beyond either outer selection edge.
    static constexpr uint8_t starts[] = {0, 4, 9, 13, 18, 22};
    static constexpr uint8_t ends[] = {3, 8, 12, 17, 21, 26};
    const GUIRect selected = getSelectionRect();
    HighlightSelection(TEXT_REGION_BODY,
                       GUIRect(anchor._x + starts[selected.Left()],
                               anchor._y + selected.Top(),
                               anchor._x + ends[selected.Right()],
                               anchor._y + selected.Bottom() + 1));
  }

  drawMasterVuMeter(Player::GetInstance(), props, true);
  drawMap();
  drawNotes();

  Player *player = Player::GetInstance();

  if (player->IsRunning()) {
    OnPlayerUpdate(PET_UPDATE);
  };
}

void TableView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {
  // Since this can be called from core1 via the Observer pattern,
  // we just need to set the update flag and let AnimationUpdate
  // handle the actual drawing on the main thread
  needsUIUpdate_ = true;
  needsNotesUpdate_ = true;
  needsPlayPositionUpdate_ = true;
}

void TableView::AnimationUpdate() {
  // First call the parent class implementation to draw the battery gauge
  ScreenView::AnimationUpdate();

  // Help is transient while ENTER is held on a command or its parameter.
  // Only transitions repaint the information region; the title is independent.
  TrackerCommand command = TrackerCommand::InstrumentCommandNone;
  if (IsButtonHeld(EPBM_ENTER) && col_ >= 0 && col_ <= 5) {
    const Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);
    command = col_ < 2   ? table.cmd1_[row_]
              : col_ < 4 ? table.cmd2_[row_]
                         : table.cmd3_[row_];
  }
  const auto helpChange =
      commandHelp_.Update(IsButtonHeld(EPBM_ENTER), command);
  if (helpChange == CommandHelpState::Change::Show) {
    GUITextProperties props;
    printHelpLegend(TEXT_REGION_INFO, command, props);
  } else if (helpChange == CommandHelpState::Change::Hide) {
    const TextRegionInfo *info =
        static_cast<AppWindow &>(w_).GetTextRegion(TEXT_REGION_INFO);
    if (info)
      ClearRegionTextRect(TEXT_REGION_INFO, 0, 0, info->columns, info->rows);
  }

  // Get player instance safely
  Player *player = Player::GetInstance();
  TableHolder *th = TableHolder::GetInstance();

  // Only process updates if we're fully initialized
  if (!viewData_ || !player || !th) {
    return;
  }

  drawMasterVuMeter(player, GUITextProperties{});

  // Handle any pending updates from OnPlayerUpdate using the consolidated flag
  // This ensures all UI drawing happens on the "main" thread (core0)
  if (needsUIUpdate_) {
    GUITextProperties props;

    // Draw notes
    drawNotes();

    // Get anchor position for drawing
    GUIPoint anchor = GetAnchor();
    GUIPoint pos = anchor;

    // Clear all cursor columns first (positions 0, 9, 18 from anchor)
    for (int i = 0; i < 3; i++) {
      pos._x = anchor._x - 1 + (i * 9);
      for (int row = 0; row < 16; row++) {
        pos._y = anchor._y + row;
        DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, " ", props);
      }
    }

    // Only update play position if player is running
    if (player->IsRunning()) {
      // Get current channel
      int channel = viewData_->songX_;
      TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
      TablePlayback &atp = TablePlayback::GetAutomationPlayback(channel);
      Table &viewTable = th->GetTable(viewData_->currentTable_);

      if (viewData_->playMode_ != PM_AUDITION) {
        SetColor(CD_ACCENT);
        if (tpb.GetTable() == &viewTable) {
          for (int i = 0; i < 3; i++) {
            int yPos = tpb.GetPlaybackPosition(i);
            if (yPos >= 0 && yPos < 16) {
              pos._x = anchor._x - 1 + (i * 9);
              pos._y = anchor._y + yPos;
              DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, ">", props);
            }
          }
        }
        if (atp.GetTable() == &viewTable) {
          for (int i = 0; i < 3; i++) {
            int yPos = atp.GetPlaybackPosition(i);
            if (yPos >= 0 && yPos < 16) {
              pos._x = anchor._x - 1 + (i * 9);
              pos._y = anchor._y + yPos;
              DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, ">", props);
            }
          }
        }
      }
    }

    // Reset the consolidated flag
    needsUIUpdate_ = false;
  }

  // Flush the window to ensure changes are displayed
  w_.Flush();
}
