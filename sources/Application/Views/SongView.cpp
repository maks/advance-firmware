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

#include "SongView.h"
#include "Application/AppWindow.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "Application/Views/BaseClasses/SelectionButtonBehavior.h"
#include "Application/Views/SampleEditorView.h"
#include "Application/Views/SelectionResampleSupport.h"
#include "Services/Midi/ControlSurface/ControlSurfaceTypes.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/Console/nanoprintf.h"
#include "System/System/System.h"
#include "UIController.h"
#include "ViewData.h"
#include <cstdint>
#include <stdlib.h>
#include <string>

namespace {
constexpr char kControlSurfaceWindowTopGlyph[] = {static_cast<char>(0xDA), 0};
constexpr char kControlSurfaceWindowMiddleGlyph[] = {static_cast<char>(0xB3),
                                                     0};
constexpr char kControlSurfaceWindowBottomGlyph[] = {static_cast<char>(0xC0),
                                                     0};
} // namespace
/****************
 Constructor
 ****************/

SongView::SongView(GUIWindow &w, ViewData *viewData) : ScreenView(w, viewData) {
  viewData_->AddObserver(*this);

  updatingChain_ = false;
  lastChain_ = 0;

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    this->lastPlayedPosition_[i] = 0;
    this->lastQueuedPosition_[i] = 0;
  }
  clipboard_.data_ = nullptr;
  selection_.active_ = false;
}

/****************
 Destructor
 ****************/

SongView::~SongView() { viewData_->RemoveObserver(*this); }

/****************
 Reset
 ****************/

void SongView::Reset() {
  updatingChain_ = false;
  lastChain_ = 0;

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastPlayedPosition_[i] = 0;
    lastQueuedPosition_[i] = 0;
  }

  clipboard_.data_ = nullptr;
  clipboard_.width_ = 0;
  clipboard_.height_ = 0;
  memset(clipboard_.storage_, 0xFF, sizeof(clipboard_.storage_));

  selection_.active_ = false;
  selection_.anchorX_ = 0;
  selection_.anchorY_ = 0;
  selection_.anchorOffset_ = 0;
  selection_.cursorX_ = 0;
  selection_.cursorY_ = 0;
  selection_.cursorOffset_ = 0;
  selection_.saveX_ = 0;
  selection_.saveY_ = 0;
  selection_.saveOffset_ = 0;
  invertBatt_ = false;
  needClear_ = false;
  needsUIUpdate_ = false;
}

void SongView::Update(Observable &o, I_ObservableData *d) {
  if ((&o != viewData_) || (d == nullptr)) {
    return;
  }

  isDirty_ = true;
}

/******************************************************
 updateChain:
        update current chain value by adding offset
        parameter
 ******************************************************/

void SongView::updateChain(int offset) {

  unsigned int chain = viewData_->UpdateSongChain(offset);
  updatingChain_ = true;
  lastChain_ = chain;
  updateX_ = viewData_->songX_;
  updateY_ = viewData_->songY_;
  isDirty_ = true;
}

/******************************************************
 updateChain:
        set current chain value to value parameter
 ******************************************************/

void SongView::setChain(unsigned char value) {
  viewData_->SetSongChain(value);
  lastChain_ = value;
  isDirty_ = true;
}

/******************************************************
 updateSongOffset:
        modify top of the page row in song view by
        adding offset parameter
 ******************************************************/

void SongView::updateSongOffset(int offset) {
  viewData_->UpdateSongOffset(offset);
  isDirty_ = true;
}

/******************************************************
 updateCursor:
        modify location of cursor in view by
        adding dx & dy parameters
 ******************************************************/

void SongView::updateCursor(int dx, int dy) {
  viewData_->UpdateSongCursor(dx, dy);
  isDirty_ = true;
}

/******************************************************
 cutPosition:
        copy current position content to clipboard &
        erase current position value
 ******************************************************/

void SongView::cutPosition() {

  // prepare selection data
  selection_.anchorX_ = viewData_->songX_;
  selection_.anchorY_ = viewData_->songY_;
  selection_.anchorOffset_ = viewData_->songOffset_;
  selection_.cursorX_ = viewData_->songX_;
  selection_.cursorY_ = viewData_->songY_;
  selection_.cursorOffset_ = viewData_->songOffset_;
  selection_.saveX_ = viewData_->songX_;
  selection_.saveY_ = viewData_->songY_;
  selection_.saveOffset_ = viewData_->songOffset_;

  // cut selection
  cutSelection();
};

/******************************************************
 pastePosition:
        set current position to last chain value if
        current step is empty
 ******************************************************/

void SongView::pasteLast() {

  // If we're on an empty spot, we past the last chain
  // otherwise we take the current chain as last

  unsigned char *c = viewData_->GetCurrentSongPointer();
  if (*c == 0xFF) {
    *c = lastChain_;
    viewData_->song_->chain_.SetUsed(*c);
    isDirty_ = true;
  } else {
    lastChain_ = *c;
  }
};

/******************************************************
 clonePosition:
        slim clone current position
 ******************************************************/

void SongView::clonePosition() {

  unsigned char *pos = viewData_->GetCurrentSongPointer();
  unsigned char current = *pos;
  if (current == 255)
    return;

  uint16_t next = viewData_->song_->chain_.GetNextFrom(current);
  if (next == NO_MORE_CHAIN)
    return;

  unsigned char *src = viewData_->song_->chain_.data_ + 16 * current;
  unsigned char *dst = viewData_->song_->chain_.data_ + 16 * next;

  for (int i = 0; i < 16; i++) {
    *dst++ = *src++;
  };

  src = viewData_->song_->chain_.transpose_ + 16 * current;
  dst = viewData_->song_->chain_.transpose_ + 16 * next;

  for (int i = 0; i < 16; i++) {
    *dst++ = *src++;
  };
  setChain((unsigned char)next);
  isDirty_ = true;
};

void SongView::extendSelection() {
  GUIRect rect = getSelectionRect();
  const int lastChannel = SONG_CHANNEL_COUNT - 1;
  if (rect.Left() > 0 || rect.Right() < lastChannel) {
    if (viewData_->songX_ < selection_.anchorX_) {
      viewData_->songX_ = 0;
      selection_.anchorX_ = lastChannel;
    } else {
      viewData_->songX_ = lastChannel;
      selection_.anchorX_ = 0;
    }
    isDirty_ = true;
  } else {
    if (viewData_->songY_ < selection_.anchorY_) {
      viewData_->songY_ = 0;
      selection_.anchorY_ = 0x17;
    } else {
      selection_.anchorY_ = 0;
      viewData_->songY_ = 0x17;
    }
    isDirty_ = true;
  }
}

/******************************************************
 OnFocus:
        called when current view is becoming active
 ******************************************************/

void SongView::OnFocus() {
  if (selection_.active_) {
    viewData_->songX_ = selection_.cursorX_;
    viewData_->songY_ = selection_.cursorY_;
    viewData_->SetSongOffset(static_cast<uint32_t>(selection_.cursorOffset_));
  }

  // eg. if the user was in master channel in mixerview and came to songview
  // we need to make sure we're not outside channel range
  if (viewData_->songX_ > SONG_CHANNEL_COUNT - 1) {
    viewData_->songX_ = 0; // default to channel 1
  }
};

void SongView::OnFocusLost() {
  if (!selection_.active_) {
    return;
  }

  selection_.cursorX_ = viewData_->songX_;
  selection_.cursorY_ = viewData_->songY_;
  selection_.cursorOffset_ = viewData_->songOffset_;
}

bool SongView::HasSelection() const { return selection_.active_; }

bool SongView::GetActiveSelectionRect(GUIRect &selectionRect) const {
  if (!HasSelection()) {
    return false;
  }

  selectionRect = getSelectionRect();
  return true;
}

GUIRect SongView::getSelectionRect() const {
  GUIRect selRect(
      selection_.anchorX_, selection_.anchorY_ + selection_.anchorOffset_,
      selection_.cursorX_, selection_.cursorY_ + selection_.cursorOffset_);

  selRect.Normalize();
  return selRect;
}

bool SongView::BuildSelectionRenderRequest(
    SelectionResampleRequest &request,
    SelectionOutputIntent outputIntent) const {
  if (!HasSelection()) {
    SelectionResampleSupport::ClearProjectSamplePathError();
    return false;
  }

  return SelectionResampleSupport::BuildSelectionRequest(
      getSelectionRect(), *viewData_, request, outputIntent);
}

void SongView::clearSelection() {
  selectionCopyPending_ = false;
  selection_.active_ = false;
  viewMode_ = VM_NORMAL;
  viewData_->songX_ = selection_.saveX_;
  viewData_->songY_ = selection_.saveY_;
  viewData_->SetSongOffset(static_cast<uint32_t>(selection_.saveOffset_));
  isDirty_ = true;
}

/******************************************************
 fillClipboard:
        fill clipboard with current selection value
 ******************************************************/

void SongView::fillClipboardData() {

  // Clear current selection data

  clipboard_.data_ = clipboard_.storage_;

  // Prepare selection related information

  GUIRect selRect = getSelectionRect();

  // Set current selection  data

  clipboard_.width_ = selRect.Width() + 1;
  clipboard_.height_ = selRect.Height() + 1;

  const int channelCount = SONG_CHANNEL_COUNT;
  unsigned char *src =
      viewData_->song_->data_ + selRect.Left() + channelCount * selRect.Top();
  unsigned char *dst = clipboard_.data_;

  for (int j = 0; j < clipboard_.height_; j++) {
    for (int i = 0; i < clipboard_.width_; i++) {
      *dst++ = *src++;
    }
    src += (channelCount - clipboard_.width_);
  }
};

/******************************************************
 copySelection:
        copy current selection to clipboard
 ******************************************************/

void SongView::copySelection() {

  fillClipboardData();
  selectionCopyPending_ = false;
  selection_.active_ = false;
  viewMode_ = VM_NORMAL;
  viewData_->songX_ = selection_.saveX_;
  viewData_->songY_ = selection_.saveY_;
  viewData_->SetSongOffset(static_cast<uint32_t>(selection_.saveOffset_));
  isDirty_ = true;
}

/******************************************************
 cutSelection:
        cut current selection to clipboard
 ******************************************************/

void SongView::cutSelection() {

  // first copy the data to clipboard

  fillClipboardData();
  selectionCopyPending_ = false;
  GUIRect selRect = getSelectionRect();
  const int channelCount = SONG_CHANNEL_COUNT;

  // now move all rows up for cut

  unsigned char *dst =
      viewData_->song_->data_ + selRect.Left() + channelCount * (selRect.Top());
  unsigned char *src = dst + channelCount * clipboard_.height_;

  int rowCount = SONG_ROW_COUNT - selRect.Bottom() - 1;

  for (int j = 0; j < rowCount; j++) {

    for (int i = 0; i < clipboard_.width_; i++) {
      *dst++ = *src++;
    }
    src += (channelCount - clipboard_.width_);
    dst += (channelCount - clipboard_.width_);
  }

  for (int j = 0; j > clipboard_.height_; j++) {
    for (int i = 0; i < clipboard_.width_; i++) {
      *dst++ = 0xFF;
    }
    dst += (channelCount - clipboard_.width_);
  };

  selection_.active_ = false;
  viewMode_ = VM_NORMAL;
  viewData_->songX_ = selection_.saveX_;
  viewData_->songY_ = selection_.saveY_;
  viewData_->SetSongOffset(static_cast<uint32_t>(selection_.saveOffset_));

  isDirty_ = true;
}

/******************************************************
 pasteSelection:
        paste clipboard content to song
 ******************************************************/

void SongView::pasteClipboard() {

  if (!clipboard_.data_)
    return;

  // Check we're not out of scope

  int width = clipboard_.width_;
  int height = clipboard_.height_;
  const int channelCount = SONG_CHANNEL_COUNT;

  if (viewData_->songX_ + width > channelCount) {
    width = channelCount - viewData_->songX_;
  }
  if (viewData_->songY_ + viewData_->songOffset_ + height > SONG_ROW_COUNT) {
    height = SONG_ROW_COUNT - viewData_->songY_ - viewData_->songOffset_;
  } else {

    // Move down from insert point

    unsigned char *dst = viewData_->song_->data_ + viewData_->songX_ +
                         (SONG_ROW_COUNT - 1) * channelCount;
    unsigned char *src = dst - height * channelCount;

    int rowCount =
        SONG_ROW_COUNT - (viewData_->songY_ + viewData_->songOffset_);

    for (int j = 0; j < rowCount; j++) {
      for (int i = 0; i < width; i++) {
        *dst++ = *src++;
      }
      dst -= (channelCount + width);
      src -= (channelCount + width);
    }
  }

  // Prepare copy pointer

  unsigned char *dst = viewData_->GetCurrentSongPointer();
  unsigned char *src = clipboard_.data_;

  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      *dst++ = *src++;
    }
    dst += (channelCount - width);
    src += (clipboard_.width_ - width);
  }

  updateCursor(0, height);
}

void SongView::unMuteAll() {

  UIController *controller = UIController::GetInstance();
  controller->UnMuteAll();
};

void SongView::toggleMute() {

  UIController *controller = UIController::GetInstance();

  int from = viewData_->songX_;
  int to = from;
  if (selection_.active_) {
    GUIRect r = getSelectionRect();
    from = r.Left();
    to = r.Right();
  };
  controller->ToggleMute(from, to);
  viewMode_ = (viewMode_ != VM_MUTEON) ? VM_MUTEON : VM_NORMAL;
};

void SongView::switchSoloMode() {

  UIController *controller = UIController::GetInstance();
  int from = viewData_->songX_;
  int to = from;
  if (selection_.active_) {
    GUIRect r = getSelectionRect();
    from = r.Left();
    to = r.Right();
  };
  controller->SwitchSoloMode(from, to, (viewMode_ != VM_SOLOON));
  viewMode_ = (viewMode_ != VM_SOLOON) ? VM_SOLOON : VM_NORMAL;
  isDirty_ = true;
};

void SongView::onStart() {
  Player *player = Player::GetInstance();
  unsigned char from = viewData_->songX_;
  unsigned char to = from;
  SongLoopRange songLoopRange;
  if (selection_.active_) {
    GUIRect r = getSelectionRect();
    from = r.Left();
    to = r.Right();
    if (player->GetSequencerMode() == SM_SONG) {
      songLoopRange = {
          .active = true,
          .startRow = static_cast<uint8_t>(r.Top()),
          .endRow = static_cast<uint8_t>(r.Bottom()),
          .startChannel = from,
          .endChannel = to,
      };
    }
  }
  player->OnSongStartButton(from, to, false, false, MSM_AUDIO, false, -1,
                            songLoopRange);
};

void SongView::startCurrentRow() {
  Player *player = Player::GetInstance();
  player->SetSequencerMode(SM_LIVE);
  player->OnSongStartButton(0, SONG_CHANNEL_COUNT - 1, false, false);
}

void SongView::startImmediate() {
  Player *player = Player::GetInstance();

  unsigned char from = viewData_->songX_;
  unsigned char to = from;
  player->OnSongStartButton(from, to, false, true);
}

void SongView::onStop() {
  Player *player = Player::GetInstance();
  unsigned char from = viewData_->songX_;
  unsigned char to = from;
  if (selection_.active_) {
    GUIRect r = getSelectionRect();
    from = r.Left();
    to = r.Right();
  }
  player->OnSongStartButton(from, to, true, false);
};

void SongView::jumpToNextSection(int direction) {

  int current = viewData_->songY_ + viewData_->songOffset_;
  const int channelCount = SONG_CHANNEL_COUNT;
  bool foundGap = false;
  for (int i = 0; i < SONG_ROW_COUNT; i++) {
    unsigned char *start =
        viewData_->song_->data_ + viewData_->songX_ + channelCount * current;
    if (foundGap && (*start != 0xFF)) {
      break;
    } else {
      if (*start == 0xFF) {
        foundGap = true;
      }
    }
    current += direction;
    if (current < 0) {
      current += SONG_ROW_COUNT;
    }
    if (current >= SONG_ROW_COUNT) {
      current -= SONG_ROW_COUNT;
    }
  }
  // If we go backwards, we stil have to go to the beginning of the block

  if (direction < 0) {
    while (current > 0) {
      unsigned char *start =
          viewData_->song_->data_ + viewData_->songX_ + channelCount * current;
      if (*start == 0xFF) {
        current++;
        break;
      };
      current--;
    };
  }

  // Update viewdata position from current

  if ((current - viewData_->songOffset_ > 0x17) ||
      (current - viewData_->songOffset_ < 0)) {
    viewData_->SetSongOffset(
        static_cast<uint32_t>(current > 4 ? current - 4 : 0));
  }
  viewData_->songY_ = current - viewData_->songOffset_;
  isDirty_ = true;
}

/******************************************************
 ProcessButtonMask:
        process button mask even coming from the main
        application window
 ******************************************************/

void SongView::ProcessButtonMask(unsigned short mask, bool pressed) {

  if (!pressed) {
    if (viewMode_ == VM_MUTEON) {
      if (mask & EPBM_NAV) {
        toggleMute();
      }
    };
    if (viewMode_ == VM_SOLOON) {
      if (mask & EPBM_NAV) {
        switchSoloMode();
      }
    };
    if (viewMode_ == VM_SELECTION &&
        GetSelectionEditAction(mask, false, EPBM_EDIT, EPBM_ENTER,
                               selectionCopyPending_) ==
            SelectionEditAction::Copy) {
      copySelection();
    }
    return;
  };

  if (mask == (EPBM_ENTER | EPBM_PLAY)) {
    SelectionResampleSupport::ClearProjectSamplePathError();
    if (!selection_.active_) {
      SelectionResampleSupport::ShowResampleError(*this,
                                                  "Select rows to resample");
      return;
    }

    SelectionResampleRequest request;
    if (!BuildSelectionRenderRequest(request,
                                     SelectionOutputIntent::ProjectSample)) {
      const char *error = SelectionResampleSupport::GetProjectSamplePathError();
      SelectionResampleSupport::ShowResampleError(
          *this, error[0] != '\0' ? error : "Selection is not playable");
      return;
    }

    if (SelectionResampleSupport::StartSelectionResample(*this, request)) {
      clearSelection();
    }
    return;
  }

  if (viewMode_ == VM_NEW) {
    if (mask == EPBM_ENTER) {
      unsigned char *current = viewData_->GetCurrentSongPointer();
      uint16_t next = viewData_->song_->chain_.GetNextFrom(*current);
      if (next != NO_MORE_CHAIN) {
        setChain((unsigned char)next);
        isDirty_ = true;
      }
      mask &= (0xFFFF - EPBM_ENTER);
    }
  }

  if (viewMode_ == VM_CLONE) {
    if ((mask & EPBM_ENTER) && (mask & EPBM_ALT)) {
      clonePosition();
      mask &= (0xFFFF - (EPBM_ENTER | EPBM_ALT));
    } else {
      viewMode_ = VM_SELECTION;
    }
  };

  if (selection_.active_) {
    viewMode_ = VM_SELECTION;
  };
  // Process selection related keys

  if (viewMode_ == VM_SELECTION) {
    if (selection_.active_ == false) {
      selection_.active_ = true;
      selection_.anchorX_ = viewData_->songX_;
      selection_.anchorY_ = viewData_->songY_;
      selection_.anchorOffset_ = viewData_->songOffset_;
      selection_.cursorX_ = viewData_->songX_;
      selection_.cursorY_ = viewData_->songY_;
      selection_.cursorOffset_ = viewData_->songOffset_;
      selection_.saveX_ = selection_.anchorX_;
      selection_.saveY_ = selection_.anchorY_;
      selection_.saveOffset_ = selection_.anchorOffset_;
    }
    processSelectionButtonMask(mask);
  } else {

    // Switch back to normal mode

    viewMode_ = VM_NORMAL;
    processNormalButtonMask(mask);
  }

  if (selection_.active_ && hasFocus_) {
    selection_.cursorX_ = viewData_->songX_;
    selection_.cursorY_ = viewData_->songY_;
    selection_.cursorOffset_ = viewData_->songOffset_;
  }
}

/******************************************************
 processNormalButtonMask:
        process button mask in the case there is no
        selection active
 ******************************************************/

void SongView::processNormalButtonMask(unsigned int mask) {

  if (mask & EPBM_EDIT) {
    // EDIT Modifier
    Player *player = Player::GetInstance();
    if (mask & EPBM_DOWN)
      updateSongOffset(View::songRowCount_);
    if (mask & EPBM_UP)
      updateSongOffset(-View::songRowCount_);
    if (mask & (EPBM_RIGHT | EPBM_LEFT)) {
      switch (player->GetSequencerMode()) {
      case SM_SONG:
        player->SetSequencerMode(SM_LIVE);
        break;
      case SM_LIVE:
        player->SetSequencerMode(SM_SONG);
        break;
      }
      isDirty_ = true;
    }
    if ((mask & EPBM_ENTER) && (!(mask & EPBM_NAV)))
      cutPosition();
    if (mask & EPBM_ALT) {
      viewMode_ = VM_CLONE;
    };
    if (mask & EPBM_NAV) {
      toggleMute();
    };
    if (mask & EPBM_PLAY) {
      if (player->GetSequencerMode() == SM_LIVE) {
        startImmediate();
      } else {
        // recording screen
        if (!player->IsRunning()) {
          switchToRecordView();
        }
      }
    }
  } else if (mask & EPBM_ENTER) {
    // ENTER modifier

    if (mask & EPBM_DOWN)
      updateChain(-0x10);
    if (mask & EPBM_UP)
      updateChain(0x10);
    if (mask & EPBM_LEFT)
      updateChain(-0x01);
    if (mask & EPBM_RIGHT)
      updateChain(0x01);
    if (mask & EPBM_ALT)
      pasteClipboard();
    if (mask == EPBM_ENTER) {
      pasteLast();
      viewMode_ = VM_NEW;
    }
    if (mask & EPBM_NAV) {
      switchSoloMode();
    };
  } else if (mask & EPBM_NAV) {
    // NAV Modifier

    if (mask & EPBM_ALT) {
      unMuteAll();
    }

    if (mask & EPBM_RIGHT) {
      unsigned char *data = viewData_->GetCurrentSongPointer();
      if (*data != 0xFF) {
        ViewType vt = VT_CHAIN;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        viewData_->currentChain_ = *data;
        SetChanged();
        NotifyObservers(&ve);
      }
    }

    if (mask & EPBM_UP) {
      ViewType vt = VT_PROJECT;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }

    if (mask & EPBM_DOWN) {
      ViewType vt = VT_MIXER;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }

    if (mask & EPBM_PLAY) {
      onStop();
    }

  } else if (mask & EPBM_ALT) {
    // ALT Modifier
    if (mask & EPBM_DOWN) {
      jumpToNextSection(1);
    }
    if (mask & EPBM_UP) {
      jumpToNextSection(-1);
    }
    if (mask & EPBM_PLAY) {
      startCurrentRow();
    }
    if (mask & EPBM_LEFT) {
      nudgeTempo(-1);
    }
    if (mask & EPBM_RIGHT) {
      nudgeTempo(1);
    }
  } else {
    // No modifier
    if (mask & EPBM_DOWN) {
      updateCursor(0, 1);
    }
    if (mask & EPBM_UP) {
      updateCursor(0, -1);
    }
    if (mask & EPBM_LEFT) {
      updateCursor(-1, 0);
    }
    if (mask & EPBM_RIGHT) {
      updateCursor(1, 0);
    }
    if (mask & EPBM_PLAY) {
      onStart();
    }
  }

  if ((!(mask & EPBM_ENTER)) && updatingChain_) {
    const int channelCount = SONG_CHANNEL_COUNT;
    unsigned char *c = viewData_->song_->data_ + updateX_ +
                       channelCount * (viewData_->songOffset_ + updateY_);
    viewData_->song_->chain_.SetUsed(*c);
    updatingChain_ = false;
  }
};

/******************************************************
 processSelectionButtonMask:
        process button mask in the case there is a
        selection active
 ******************************************************/

void SongView::processSelectionButtonMask(unsigned int mask) {
  const SelectionEditAction editAction =
      GetSelectionEditAction(static_cast<uint16_t>(mask), true, EPBM_EDIT,
                             EPBM_ENTER, selectionCopyPending_);
  if (editAction == SelectionEditAction::Cut) {
    cutSelection();
    return;
  }

  if (mask & EPBM_EDIT) {
    // EDIT Modifier
    if (mask & EPBM_NAV) {
      toggleMute();
    };
    if (mask & EPBM_ALT) {
      extendSelection();
    };
  } else if (mask & EPBM_ENTER) {
    // ENTER modifier
    if (mask & EPBM_ALT) {
      cutSelection();
    }
    if (mask & EPBM_NAV) {
      switchSoloMode();
    };
  } else if (mask & EPBM_NAV) {
    // NAV Modifier
    if (mask & EPBM_ALT) {
      unMuteAll();
    }

    if (mask & EPBM_RIGHT) {
      unsigned char *data = viewData_->GetCurrentSongPointer();
      if (*data != 0xFF) {
        ViewType vt = VT_CHAIN;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        viewData_->currentChain_ = *data;
        SetChanged();
        NotifyObservers(&ve);
      }
    }

    if (mask & EPBM_UP) {
      ViewType vt = VT_PROJECT;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }

    if (mask & EPBM_DOWN) {
      ViewType vt = VT_MIXER;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }

    if (mask & EPBM_PLAY) {
      onStop();
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
      onStart();
    }
  }
}

void SongView::drawSelection() {
  // Prepare selection related information
  if (selection_.active_) {
    const GUIPoint anchor = GetAnchor();
    const GUIRect selRect = getSelectionRect();
    // The song selection uses absolute song rows; only visible rows belong
    // to this region. Channel separators are inside the selected rectangle.
    const int top = std::max(0, selRect.Top() - int(viewData_->songOffset_));
    const int bottom =
        std::min(int(View::songRowCount_),
                 selRect.Bottom() - int(viewData_->songOffset_) + 1);
    HighlightSelection(TEXT_REGION_BODY,
                       GUIRect(anchor._x + 3 * selRect.Left(), anchor._y + top,
                               anchor._x + 3 * selRect.Right() + 2,
                               anchor._y + bottom));
  }
}

/******************************************************
 Redraw:
        redraw completely the song view
 ******************************************************/

void SongView::DrawView() {

  Clear();

  GUITextProperties props;
  GUIPoint pos = GetTitlePosition();

  // Draw title
  Player *player = Player::GetInstance();
  const char *modeLabel =
      ((player->GetSequencerMode() == SM_SONG) ? "Song" : "Live");
  Variable *v = viewData_->project_->FindVariable(FourCC::VarProjectName);
  etl::string<MAX_PROJECT_NAME_LENGTH> projectName = v->GetString();
  etl::string<23> title(modeLabel);
  title += ' ';
  title += projectName;
  setTitle(title);

  // Draw hint for Live mode: EDIT+LEFT back to Song mode
  if (player->GetSequencerMode() == SM_LIVE) {
    SetColor(CD_INFO);
    GUITextProperties props;
    pos._y = 0;
    DrawRegionString(TEXT_REGION_INFO, 0, 0, "(EDIT+LEFT back to Song)", props);
  }

  // Compute song grid location
  GUIPoint anchor = GetAnchor();

  // Align small labels to the nearest cell above the normal-sized channels.
  const auto &window = static_cast<AppWindow &>(w_);
  const TextRegionInfo *body = window.GetTextRegion(TEXT_REGION_BODY);
  const TextRegionInfo *info = window.GetTextRegion(TEXT_REGION_INFO);
  const auto informationColumn = [body, info](int column) {
    if (!body || !info)
      return -1;
    const int pixel =
        body->definition.x + column * body->glyph_width - info->definition.x;
    return (pixel + info->glyph_width / 2) / info->glyph_width;
  };

  // Draw MIDI input assignments directly above their reserved song channels.
  // Unassigned columns stay blank and the row is informational only.
  bool hasMidiInputAssignment = false;
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    if (viewData_->song_->IsMidiInputChannelAssigned(i)) {
      hasMidiInputAssignment = true;
      break;
    }
  }

  if (hasMidiInputAssignment) {
    SetColor(CD_HILITE2);
    DrawRegionString(TEXT_REGION_INFO, informationColumn(anchor._x - 3), 1,
                     "IN", props);
  }
  char midiInputChannel[3];
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    const uint8_t assignment = viewData_->song_->GetMidiInputChannel(i);
    if (assignment != MIDI_INPUT_CHANNEL_NONE) {
      SetColor(CD_ACCENT);
      hex2char(assignment, midiInputChannel);
      DrawRegionString(TEXT_REGION_INFO, informationColumn(anchor._x + (3 * i)),
                       1, midiInputChannel, props);
    }
  }

  // Display row numbers
  SetColor(CD_HILITE1);
  char row[3];
  pos = anchor;
  pos._x -= 3;
  int32_t controlSurfaceSongWindowStart = -1;
  int32_t controlSurfaceSongWindowEnd = -1;
  MidiService *midiService = MidiService::GetInstance();
  if (usbHostModeActive() && (player != nullptr) &&
      (player->GetSequencerMode() == SM_LIVE) && (midiService != nullptr) &&
      midiService->HasControlSurfaceSongWindow()) {
    controlSurfaceSongWindowStart =
        static_cast<int32_t>(midiService->GetControlSurfaceSongWindowOffset());
    controlSurfaceSongWindowEnd =
        controlSurfaceSongWindowStart +
        static_cast<int32_t>(
            midiService->GetControlSurfaceSongWindowRowCount()) -
        1;
  }
  const int32_t controlSurfaceIndicatorX = anchor._x - 4;
  for (int j = 0; j < View::songRowCount_; j++) {
    const int32_t songRow = j + viewData_->songOffset_;
    if ((songRow >= controlSurfaceSongWindowStart) &&
        (songRow <= controlSurfaceSongWindowEnd)) {
      const char *indicatorGlyph = kControlSurfaceWindowMiddleGlyph;
      if (songRow == controlSurfaceSongWindowStart) {
        indicatorGlyph = kControlSurfaceWindowTopGlyph;
      } else if (songRow == controlSurfaceSongWindowEnd) {
        indicatorGlyph = kControlSurfaceWindowBottomGlyph;
      }
      SetColor(CD_HILITE2);
      DrawRegionString(TEXT_REGION_BODY, controlSurfaceIndicatorX, pos._y,
                       indicatorGlyph, props);
    }

    const uint8_t rowValue = static_cast<uint8_t>(songRow);
    ((songRow / ALT_ROW_NUMBER) % 2) ? SetColor(CD_ACCENT)
                                     : SetColor(CD_ACCENTALT);
    hex2char(rowValue, row);
    DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, row, props);
    pos._y += 1;
  }

  SetColor(CD_NORMAL);

  pos = anchor;
  const int channelCount = SONG_CHANNEL_COUNT;
  unsigned char *data =
      viewData_->song_->data_ + (channelCount * viewData_->songOffset_);
  short dx = 3;
  short dy = 1;
  for (int j = 0; j < View::songRowCount_; j++) {

    pos._x = anchor._x;

    for (int i = 0; i < channelCount; i++) {

      bool invert = false;

      // see if we need to invert current step
      // The cursor is inverted here; block selection styling follows below.

      if (!selection_.active_ && i == viewData_->songX_ &&
          j == viewData_->songY_) {
        invert = true;
      }

      // draw current step
      unsigned char d = *data++;
      if (viewData_->song_->IsMidiInputChannelAssigned(i)) {
        SetColor(CD_ACCENTALT);
      } else if (d == 0xFE) {
        SetColor(CD_ACCENT);
      } else if (d == 0x00) {
        SetColor(CD_HILITE1);
      } else {
        SetColor(CD_NORMAL);
      }

      if (invert) {
        SetColor(CD_HILITE2);
        props.invert_ = true;
      }
      if (d == 0xFF) {
        DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, "--", props);
      } else {
        hex2char(d, row);
        DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, row, props);
      }

      // Put back drawing state

      if (invert) {
        SetColor(CD_NORMAL);
        props.invert_ = false;
      }

      // Next step

      pos._x += dx;
    }
    pos._y += dy;
  }
  SetColor(CD_NORMAL);

  drawSelection();

  drawMasterVuMeter(Player::GetInstance(), props, true);

  drawMap();
  drawNotes();

  if (player->IsRunning()) {
    OnPlayerUpdate(PET_UPDATE);
  };
};

/******************************************************
 OnPlayterUpdate:
        Called when positions in player change. Should
        provide visual feedback of currently played
        position
 ******************************************************/

void SongView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {
  // Since this can be called from core1 via the Observer pattern,
  // we need to ensure we don't call any drawing functions directly
  // Instead of drawing directly, we'll just update our state and let
  // AnimationUpdate handle the actual drawing
  SyncMaster *sync = SyncMaster::GetInstance();
  if ((eventType == PET_UPDATE) && (!sync->MajorSlice()))
    return;

  // Set the consolidated flag for UI updates
  needsUIUpdate_ = true;

  ScreenView::OnPlayerUpdate(eventType, tick);
};

void SongView::AnimationUpdate() {
  // First call the parent class implementation to draw the battery gauge
  ScreenView::AnimationUpdate();

  // Get player instance safely
  Player *player = Player::GetInstance();
  // Only process updates if we're fully initialized
  if (!viewData_ || !player) {
    return;
  }

  // Handle any pending updates from OnPlayerUpdate
  // This ensures all UI drawing happens on the "main" thread (core0)
  GUITextProperties props;

  // Always update VU meter even if other parts of UI dont need updating
  drawMasterVuMeter(player, props);

  // Use the consolidated flag for all UI updates
  if (needsUIUpdate_) {
    drawNotes();

    // Handle position updates
    GUIPoint anchor = GetAnchor();
    GUIPoint pos = anchor;
    pos._x -= 1;

    SetColor(CD_CURSOR);

    // Loop on all channels
    const int channelCount = SONG_CHANNEL_COUNT;
    for (int i = 0; i < channelCount; i++) {
      // Clear all current positions
      int y = lastPlayedPosition_[i] - viewData_->songOffset_;
      if (y >= 0 && y < View::songRowCount_) {
        pos._y = anchor._y + y;
        DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, " ", props);
      }

      // Clear all last queued positions
      y = lastQueuedPosition_[i] - viewData_->songOffset_;
      if (y >= 0 && y < View::songRowCount_) {
        pos._y = anchor._y + y;
        DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, " ", props);
      }

      // For each playing position, draw current location
      if (player->IsChannelPlaying(i)) {
        if (viewData_->currentPlayChain_[i] != 0xFF) {
          int y = viewData_->songPlayPos_[i] - viewData_->songOffset_;
          if (y >= 0 && y < View::songRowCount_ &&
              viewData_->playMode_ != PM_AUDITION) {
            pos._y = anchor._y + y;
            if (!player->IsChannelMuted(i)) {
              SetColor(CD_ACCENT);
              DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, ">", props);
            } else {
              SetColor(CD_ACCENTALT);
              DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, "-", props);
            }
            SetColor(CD_CURSOR);
            lastPlayedPosition_[i] = viewData_->songPlayPos_[i];
          }
        }
      }

      // If in live mode, update queued position
      if (player->GetSequencerMode() == SM_LIVE) {
        if (player->GetQueueingMode(i) != QM_NONE) {
          int y = player->GetQueuePosition(i) - viewData_->songOffset_;
          if (y >= 0 && y < View::songRowCount_) {
            pos._y = anchor._y + y;
            const char *indicator = player->GetLiveIndicator(i);
            if ((indicator[0] == '-') || (indicator[0] == '_')) {
              SetColor(CD_ACCENTALT);
            } else {
              SetColor(CD_ACCENT);
            }
            DrawRegionString(TEXT_REGION_BODY, pos._x, pos._y, indicator,
                             props);
            SetColor(CD_CURSOR);
            lastQueuedPosition_[i] = player->GetQueuePosition(i);
          }
        }
      }
      pos._x += 3;
    }

    // Playback markers occupy channel separators inside a block selection.
    // Keep their glyphs, but restore selection styling after these updates.
    drawSelection();

    // Reset the consolidated flag
    needsUIUpdate_ = false;
  }

  // Flush the window to ensure changes are displayed
  w_.Flush();
}

void SongView::nudgeTempo(int direction) {
  ApplicationCommandDispatcher *dispatcher =
      ApplicationCommandDispatcher::GetInstance();
  switch (direction) {
  case -1:
    dispatcher->OnNudgeDown();
    break;
  case 1:
    dispatcher->OnNudgeUp();
    break;
  }
};
