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

#ifndef _SONG_VIEW_H_
#define _SONG_VIEW_H_

#include "ScreenView.h"

class SongView;
struct SelectionResampleRequest;

class SongView : public ScreenView, public I_Observer {
public:
  // The normal body starts below the two small-font information rows.
  GUIPoint GetAnchor() override { return GUIPoint(5, 0); }

  SongView(GUIWindow &w, ViewData *viewData);
  ~SongView() override;
  void Reset();

  // View implementation
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();
  virtual void OnFocusLost();
  virtual void AnimationUpdate();
  void Update(Observable &o, I_ObservableData *d) override;
  bool HasSelection() const;
  bool GetActiveSelectionRect(GUIRect &selectionRect) const;
  bool BuildSelectionRenderRequest(SelectionResampleRequest &request,
                                   SelectionOutputIntent outputIntent) const;

protected:
  void processNormalButtonMask(unsigned int mask);
  void processSelectionButtonMask(unsigned int mask);

  void extendSelection();
  void updateChain(int offset);
  void updateSongOffset(int offset);
  void updateCursor(int dx, int dy);
  void setChain(unsigned char);
  void cutPosition();
  void clonePosition();
  void pasteLast();
  void fillClipboardData();
  GUIRect getSelectionRect() const;
  void clearSelection();
  void copySelection();
  void pasteClipboard();
  void cutSelection();

  void unMuteAll();
  void toggleMute();
  void switchSoloMode();

  void onStart();
  void startCurrentRow();
  void startImmediate();
  void onStop();

  void jumpToNextSection(int dir);

  void nudgeTempo(int direction);

private:
  void drawSelection();
  bool shouldDrawPlayTime() const override { return true; }

  bool updatingChain_; // .Flag that tells we're updating chain
                       //  so we don't allocate chains while
                       //  doing multiple A+ARROWS

  int updateX_; // . Position where update is happening
  int updateY_; //

  unsigned char lastChain_; // .Last chain clipboard

  int lastPlayedPosition_[SONG_CHANNEL_COUNT]; // .Last position played for song
                                               //  used for drawing purpose

  int lastQueuedPosition_[SONG_CHANNEL_COUNT]; // .Last live queued position for
                                               // song
                                               //  used for drawing purpose

  struct { // .Clipboard structure
    unsigned char *data_;
    unsigned char storage_[SONG_CHANNEL_COUNT * SONG_ROW_COUNT];
    int width_;
    int height_; // .
  } clipboard_;

  struct {
    bool active_;
    int anchorX_;
    int anchorY_;
    int anchorOffset_;
    int cursorX_;
    int cursorY_;
    int cursorOffset_;
    int saveX_;
    int saveY_;
    int saveOffset_;
  } selection_;

  bool selectionCopyPending_ = false;

  bool invertBatt_;
  bool needClear_;

  // Flags to track which UI elements need updating
  // These prevent core1 from directly updating the UI
  bool needsUIUpdate_ = false; // Single flag for all UI updates (notes, VU
                               // meter, positions, play time)
};

#endif
