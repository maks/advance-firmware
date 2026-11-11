/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "FM6PresetBrowserView.h"
#include "Application/Instruments/AdvanceFM6/FM6Dx7Patch.h"
#include "Application/Instruments/FM6Instrument.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Player/Player.h"
#include "Application/Views/BaseClasses/BrowserPaging.h"
#include "Application/Views/InstrumentImportView.h"
#include "Externals/etl/include/etl/string.h"
#include "ModalDialogs/MessageBox.h"

// -4 to allow for title, footer & spacers, matching the file browser.
#define LIST_PAGE_SIZE (SCREEN_HEIGHT - 4)

namespace {

// Number of built-in banks. The preset table is laid out as consecutive
// Dx7PresetBankSize-voice display banks and every bank is full: the generator
// emits whole 32-voice source banks, each split into two 16-voice banks
size_t builtInBankCount() {
  return fm6::BuiltInPatchCount() / fm6::Dx7PresetBankSize;
}

} // namespace

FM6PresetBrowserView::FM6PresetBrowserView(GUIWindow &w, ViewData *viewData)
    : ScreenView(w, viewData) {
  toInstrID_ = viewData_->currentInstrumentID_;
}

FM6PresetBrowserView::~FM6PresetBrowserView() {}

void FM6PresetBrowserView::Reset() {
  toInstrID_ = 0;
  bankRow_ = 1;
  patchIndex_ = 0;
  patchTop_ = 0;
  patchFocus_ = false;
  previewKeyHeld_ = false;
}

void FM6PresetBrowserView::OnFocus() {
  toInstrID_ = viewData_->currentInstrumentID_;
  // Start on the first bank (skipping [SDCard]) so patches are visible right
  // away, with focus on the bank column.
  bankRow_ = builtInBankCount() > 0 ? 1 : 0;
  patchIndex_ = 0;
  patchTop_ = 0;
  patchFocus_ = false;
  previewKeyHeld_ = false;
  isDirty_ = true;
}

void FM6PresetBrowserView::OnPlayerUpdate(PlayerEventType, unsigned int tick) {}

void FM6PresetBrowserView::DrawView() {
  Clear();
  setTitle(etl::string<23>("FM6 Presets"));

  GUITextProperties props;
  const int bankX = 1;
  // Bank names are capped at 12 chars, so a fixed 13-char offset leaves the
  // patch column clear of the widest bank name plus a one-char gap.
  const int patchX = bankX + 13;
  const int yTop = 1;
  const size_t bankCount = builtInBankCount();

  // Left column: [SDCard] followed by each bank. This always fits without
  // scrolling (bankCount + 1 rows <= LIST_PAGE_SIZE). The currently selected
  // bank is highlighted; it shows the active cursor colour only while the bank
  // column holds focus, otherwise a dimmer highlight marks it.
  for (size_t row = 0; row <= bankCount; row++) {
    const int y = yTop + static_cast<int>(row);
    const bool selected = (row == bankRow_);
    if (selected && !patchFocus_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else if (selected) {
      SetColor(CD_HILITE1);
      props.invert_ = false;
    } else {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    }
    if (row == 0) {
      DrawRegionString(TEXT_REGION_BODY, bankX, y, "[SDCard]", props);
    } else {
      DrawRegionString(TEXT_REGION_BODY, bankX, y,
                       fm6::GetBuiltInPatchBankName(static_cast<int32_t>(
                           (row - 1) * fm6::Dx7PresetBankSize)),
                       props);
    }
  }

  // Right column: the selected bank's patches (empty for the [SDCard] row).
  if (bankRow_ == 0) {
    return;
  }
  const size_t bankIndex = bankRow_ - 1;
  const size_t count = fm6::Dx7PresetBankSize;
  // Offset the patch column down one row so its first entry doesn't sit on the
  // same line as the [SDCard] entry at the top of the bank column.
  for (size_t i = patchTop_; i < patchTop_ + LIST_PAGE_SIZE && i < count; i++) {
    const int y = yTop + 1 + static_cast<int>(i - patchTop_);
    if (i == patchIndex_ && patchFocus_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    }
    const size_t program = bankIndex * fm6::Dx7PresetBankSize + i;
    // GetBuiltInPatchName returns a shared scratch buffer; DrawString copies it
    // before the next iteration overwrites it.
    DrawRegionString(TEXT_REGION_BODY, patchX, y,
                     fm6::GetBuiltInPatchName(static_cast<int32_t>(program)),
                     props);
  }
}

void FM6PresetBrowserView::ProcessButtonMask(unsigned short mask,
                                             bool pressed) {
  if (!pressed) {
    if (previewKeyHeld_ && !(mask & EPBM_PLAY)) {
      stopPatchPreview();
    }
    return;
  }

  const size_t bankCount = builtInBankCount();

  // NAV + LEFT leaves the browser back to the instrument view.
  if ((mask & EPBM_LEFT) && (mask & EPBM_NAV)) {
    exitToInstrumentView();
    return;
  }

  if (mask & EPBM_PLAY) {
    // Auditions the highlighted patch while held (patch column only).
    if (patchFocus_ && bankRow_ > 0) {
      const size_t program =
          (bankRow_ - 1) * fm6::Dx7PresetBankSize + patchIndex_;
      startPatchPreview(
          fm6::GetBuiltInPatchRecord(static_cast<int32_t>(program)));
    }
    return;
  }

  if (mask & EPBM_UP) {
    if (patchFocus_) {
      const bool page = (mask & EPBM_EDIT) != 0;
      BrowserPaging::Warp(patchIndex_, patchTop_, fm6::Dx7PresetBankSize,
                          LIST_PAGE_SIZE, true, page);
    } else if (bankRow_ > 0) {
      bankRow_--;
      patchIndex_ = 0;
      patchTop_ = 0;
    }
    isDirty_ = true;
    return;
  }

  if (mask & EPBM_DOWN) {
    if (patchFocus_) {
      const bool page = (mask & EPBM_EDIT) != 0;
      BrowserPaging::Warp(patchIndex_, patchTop_, fm6::Dx7PresetBankSize,
                          LIST_PAGE_SIZE, false, page);
    } else if (bankRow_ < bankCount) {
      bankRow_++;
      patchIndex_ = 0;
      patchTop_ = 0;
    }
    isDirty_ = true;
    return;
  }

  if (mask & EPBM_RIGHT) {
    // Move focus into the patch column (not available on the [SDCard] row).
    if (!patchFocus_ && bankRow_ > 0) {
      patchFocus_ = true;
      isDirty_ = true;
    }
    return;
  }

  if (mask & EPBM_LEFT) {
    // Move focus back to the bank column.
    if (patchFocus_) {
      stopPatchPreview();
      patchFocus_ = false;
      isDirty_ = true;
    }
    return;
  }

  if (mask & EPBM_ENTER) {
    stopPatchPreview();
    if (bankRow_ == 0) {
      // The [SDCard] entry hands off to the DX7 .syx file importer.
      openSysexFileBrowser();
      return;
    }
    if (!patchFocus_) {
      // Enter on a bank dives into its patch list.
      patchFocus_ = true;
      isDirty_ = true;
      return;
    }
    const size_t program =
        (bankRow_ - 1) * fm6::Dx7PresetBankSize + patchIndex_;
    commitBuiltInPatch(program);
  }
}

void FM6PresetBrowserView::commitBuiltInPatch(size_t presetIndex) {
  stopPatchPreview();
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(toInstrID_);
  if (instrument == nullptr || instrument->GetType() != IT_FM6) {
    MessageBox *dialog =
        MessageBox::Create(*this, "Not a FM6 instrument", MBBF_OK);
    DoModal(dialog);
    return;
  }
  FM6Instrument *fm6Instrument = static_cast<FM6Instrument *>(instrument);
  fm6Instrument->SetBuiltInPatch(static_cast<int32_t>(presetIndex));
  fm6Instrument->SetName(
      fm6::GetBuiltInPatchName(static_cast<int32_t>(presetIndex)));
  exitToInstrumentView();
}

void FM6PresetBrowserView::startPatchPreview(const uint8_t *record) {
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(toInstrID_);
  if (instrument == nullptr || instrument->GetType() != IT_FM6 ||
      viewData_->song_ == nullptr) {
    return;
  }
  if (previewKeyHeld_) {
    stopPatchPreview();
  }
  const int channelCount = SONG_CHANNEL_COUNT;
  if (channelCount <= 0) {
    return;
  }
  static_cast<FM6Instrument *>(instrument)->SetPreviewPatch(record);
  previewChannel_ = static_cast<unsigned short>(channelCount - 1);
  Player::GetInstance()->PlayNote(static_cast<unsigned short>(toInstrID_),
                                  previewChannel_, 60, 0x7F);
  previewKeyHeld_ = true;
}

void FM6PresetBrowserView::stopPatchPreview() {
  if (!previewKeyHeld_) {
    return;
  }
  Player::GetInstance()->StopNote(static_cast<unsigned short>(toInstrID_),
                                  previewChannel_);
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(toInstrID_);
  if (instrument != nullptr && instrument->GetType() == IT_FM6) {
    static_cast<FM6Instrument *>(instrument)->ClearPreviewPatch();
  }
  previewKeyHeld_ = false;
}

void FM6PresetBrowserView::openSysexFileBrowser() {
  stopPatchPreview();
  InstrumentImportView::SetMode(InstrumentImportView::Mode::Dx7Patch);
  ViewType vt = VT_INSTRUMENT_IMPORT;
  ViewEvent ve(VET_SWITCH_VIEW, &vt);
  SetChanged();
  NotifyObservers(&ve);
}

void FM6PresetBrowserView::exitToInstrumentView() {
  stopPatchPreview();
  ViewType vt = VT_INSTRUMENT;
  ViewEvent ve(VET_SWITCH_VIEW, &vt);
  SetChanged();
  NotifyObservers(&ve);
}
