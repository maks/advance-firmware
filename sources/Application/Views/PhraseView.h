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

#ifndef _PHRASE_VIEW_H_
#define _PHRASE_VIEW_H_

#include "BaseClasses/CommandHelpState.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "ScreenView.h"
#include "ViewData.h"

struct SelectionResampleRequest;

class PhraseView : public ScreenView {

public:
  // The normal body starts below the two small-font information rows.
  GUIPoint GetAnchor() override { return GUIPoint(5, 0); }

  PhraseView(GUIWindow &w, ViewData *viewData);
  ~PhraseView();
  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();
  virtual void AnimationUpdate();

protected:
  void updateCursor(int dx, int dy);
  void updateCursorValue(ViewUpdateDirection offset, int xOffset = 0,
                         int yOffset = 0, int rowOffset = 0);
  void updateSelectionValue(ViewUpdateDirection direction);
  void warpToNeighbour(int offset);
  void warpInChain(int offset);
  void cutPosition();
  void pasteLast();

  void extendSelection();
  bool interpolateSelection();

  GUIRect getSelectionRect() const;
  bool buildSelectionResampleRequest(SelectionResampleRequest &request) const;
  void clearSelection();
  void fillClipboardData();
  void copySelection();
  void cutSelection();
  void pasteClipboard();

  void startAudition(bool startIfNotRunning);
  void stopAudition();
  void unMuteAll();
  void toggleMute();
  void switchSoloMode();

  void processNormalButtonMask(unsigned short mask);
  void processSelectionButtonMask(unsigned short mask);

  void setTextProps(GUITextProperties &props, int row, int col, bool restore);
  bool getEffectiveInstrumentForRow(int row, uint8_t &instrumentId) const;

private:
  CommandHelpState commandHelp_;
  void drawInstrumentInfo();
  enum class PhraseClipboardColumnType : uint8_t {
    Note,
    Instrument,
    Command,
    Parameter,
    Invalid,
  };

  int row_;
  int col_;
  int lastNote_;
  int lastInstr_;
  int lastCmd_;
  int lastParam_;
  Phrase *phrase_;
  int lastPlayingPos_;
  Variable cmdEdit_;
  GUIPoint cmdEditPos_;
  UIBigHexVarField cmdEditField_;

  struct clipboard {
    bool active_;
    int col_;
    int row_;
    int width_;
    int height_;
    uchar note_[16];
    uchar instr_[16];
    uchar cmd1_[16];
    ushort param1_[16];
    uchar cmd2_[16];
    ushort param2_[16];
    PhraseClipboardColumnType columnTypes_[6];
  } clipboard_;

  int saveCol_;
  int saveRow_;
  bool selectionCopyPending_ = false;

  static short offsets_[2][4];

  // Flags to track which UI elements need updating
  // These prevent core1 from directly updating the UI
  bool needsUIUpdate_ = false; // Single flag for all UI updates (notes, VU
                               // meter, positions, live indicators)

  bool needsLiveIndicatorUpdate_ = false;
};

#endif
