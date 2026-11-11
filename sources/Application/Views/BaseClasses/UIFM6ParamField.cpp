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

#include "UIFM6ParamField.h"

#include "Application/AppWindow.h"
#include "Application/Utils/char.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "ViewUtils.h"
#include <System/Console/nanoprintf.h>
#include <algorithm>

namespace {
// DX7 break point 0 is A-1, which is MIDI note 21.
constexpr int kBreakPointNoteOffset = 21;

// Padded to a common width so a shorter name never leaves the tail of a
// longer one behind it on screen.
const char *const kCurveNames[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
const char *const kModeNames[] = {"ratio", "fixed"};
const char *const kOnOffNames[] = {"off", "on "};
// DX7 LFO wave order: triangle, saw down, saw up, square, sine, sample and
// hold. Two of these have no dedicated glyph. The font only has a rising saw,
// so both saws share it and an arrow gives the direction; sample and hold is
// drawn as a half-block step, a high held level followed by a low one, since
// no staircase glyph exists (every box-drawing stroke sits at mid height).
// Every entry is three columns so the field keeps its width whichever wave is
// selected.
const char *const kLfoWaveNames[] = {
    char_waveform_tri_s " ",
    char_waveform_saw_s char_indicator_down_s,
    char_waveform_saw_s char_indicator_up_s,
    char_waveform_pulse_s " ",
    char_waveform_sine_s " ",
    char_block_top_s char_block_bottom_s " ",
};
} // namespace

// Operator slice (17 bytes), all offsets relative to the operator's slice.
const FM6ParamDef kFM6OpRate[4] = {
    {.offset = 0, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 1, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 2, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 3, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
};
const FM6ParamDef kFM6OpLevel[4] = {
    {.offset = 4, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 5, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 6, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 7, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
};
const FM6ParamDef kFM6OpBreakPoint = {.offset = 8,
                                      .valueMask = 0x7F,
                                      .max = 99,
                                      .coarseStep = 12,
                                      .format = FM6ParamFormat::Note};
const FM6ParamDef kFM6OpLeftDepth = {
    .offset = 9, .valueMask = 0x7F, .max = 99, .coarseStep = 10};
const FM6ParamDef kFM6OpRightDepth = {
    .offset = 10, .valueMask = 0x7F, .max = 99, .coarseStep = 10};
const FM6ParamDef kFM6OpLeftCurve = {.offset = 11,
                                     .valueMask = 0x03,
                                     .max = 3,
                                     .coarseStep = 1,
                                     .format = FM6ParamFormat::Names,
                                     .names = kCurveNames};
const FM6ParamDef kFM6OpRightCurve = {.offset = 11,
                                      .shift = 2,
                                      .valueMask = 0x03,
                                      .max = 3,
                                      .coarseStep = 1,
                                      .format = FM6ParamFormat::Names,
                                      .names = kCurveNames};
const FM6ParamDef kFM6OpRateScaling = {
    .offset = 12, .valueMask = 0x07, .max = 7, .coarseStep = 1};
// Raw 0-14 maps to a displayed -7..+7.
const FM6ParamDef kFM6OpDetune = {.offset = 12,
                                  .shift = 3,
                                  .valueMask = 0x0F,
                                  .max = 14,
                                  .coarseStep = 1,
                                  .format = FM6ParamFormat::Signed,
                                  .displayOffset = -7};
const FM6ParamDef kFM6OpAmpModSens = {
    .offset = 13, .valueMask = 0x03, .max = 3, .coarseStep = 1};
const FM6ParamDef kFM6OpVelSens = {
    .offset = 13, .shift = 2, .valueMask = 0x07, .max = 7, .coarseStep = 1};
const FM6ParamDef kFM6OpOutputLevel = {
    .offset = 14, .valueMask = 0x7F, .max = 99, .coarseStep = 10};
const FM6ParamDef kFM6OpFixedMode = {.offset = 15,
                                     .valueMask = 0x01,
                                     .max = 1,
                                     .coarseStep = 1,
                                     .format = FM6ParamFormat::Names,
                                     .names = kModeNames};
const FM6ParamDef kFM6OpCoarse = {
    .offset = 15, .shift = 1, .valueMask = 0x1F, .max = 31, .coarseStep = 10};
const FM6ParamDef kFM6OpFine = {
    .offset = 16, .valueMask = 0x7F, .max = 99, .coarseStep = 10};

// Global block (16 bytes), starting at packed record byte 102.
const FM6ParamDef kFM6PitchEgRate[4] = {
    {.offset = 0, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 1, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 2, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 3, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
};
const FM6ParamDef kFM6PitchEgLevel[4] = {
    {.offset = 4, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 5, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 6, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
    {.offset = 7, .valueMask = 0x7F, .max = 99, .coarseStep = 10},
};
const FM6ParamDef kFM6Feedback = {
    .offset = 9, .valueMask = 0x07, .max = 7, .coarseStep = 1};
const FM6ParamDef kFM6OscSync = {.offset = 9,
                                 .shift = 3,
                                 .valueMask = 0x01,
                                 .max = 1,
                                 .coarseStep = 1,
                                 .format = FM6ParamFormat::Names,
                                 .names = kOnOffNames};
const FM6ParamDef kFM6LfoSpeed = {
    .offset = 10, .valueMask = 0x7F, .max = 99, .coarseStep = 10};
const FM6ParamDef kFM6LfoDelay = {
    .offset = 11, .valueMask = 0x7F, .max = 99, .coarseStep = 10};
const FM6ParamDef kFM6LfoPitchDepth = {
    .offset = 12, .valueMask = 0x7F, .max = 99, .coarseStep = 10};
const FM6ParamDef kFM6LfoAmpDepth = {
    .offset = 13, .valueMask = 0x7F, .max = 99, .coarseStep = 10};
const FM6ParamDef kFM6LfoSync = {.offset = 14,
                                 .valueMask = 0x01,
                                 .max = 1,
                                 .coarseStep = 1,
                                 .format = FM6ParamFormat::Names,
                                 .names = kOnOffNames};
const FM6ParamDef kFM6LfoWave = {.offset = 14,
                                 .shift = 1,
                                 .valueMask = 0x07,
                                 .max = 5,
                                 .coarseStep = 1,
                                 .format = FM6ParamFormat::Names,
                                 .names = kLfoWaveNames};
const FM6ParamDef kFM6PitchModSens = {
    .offset = 14, .shift = 4, .valueMask = 0x07, .max = 7, .coarseStep = 1};
// Raw 0-48 maps to a displayed -24..+24.
const FM6ParamDef kFM6Transpose = {.offset = 15,
                                   .valueMask = 0x7F,
                                   .max = 48,
                                   .coarseStep = 12,
                                   .format = FM6ParamFormat::Signed,
                                   .displayOffset = -24};

UIFM6ParamField::UIFM6ParamField(const GUIPoint &position,
                                 FM6Instrument &instrument,
                                 const uint8_t &selection,
                                 const FM6ParamDef &def)
    : UIField(position), instrument_(instrument), selection_(selection),
      def_(def){};

size_t UIFM6ParamField::sliceIndex() const {
  if (selection_ >= FM6SelectionGlobal || selection_ == 0) {
    return FM6Instrument::GlobalSliceIndex;
  }
  // Slices are in the packed record's physical order, which runs from DX7
  // operator 6 down to operator 1.
  return fm6::OperatorCount - selection_;
}

FourCC UIFM6ParamField::sliceID() const {
  switch (sliceIndex()) {
  case 0:
    return FourCC::FM6InstrumentOp1Params;
  case 1:
    return FourCC::FM6InstrumentOp2Params;
  case 2:
    return FourCC::FM6InstrumentOp3Params;
  case 3:
    return FourCC::FM6InstrumentOp4Params;
  case 4:
    return FourCC::FM6InstrumentOp5Params;
  case 5:
    return FourCC::FM6InstrumentOp6Params;
  default:
    return FourCC::FM6InstrumentGlobalParams;
  }
}

uint8_t UIFM6ParamField::read() const {
  const uint8_t byte = instrument_.GetPatchByte(sliceIndex(), def_.offset);
  return (byte >> def_.shift) & def_.valueMask;
}

void UIFM6ParamField::write(uint8_t value) {
  const size_t slice = sliceIndex();
  const uint8_t byte = instrument_.GetPatchByte(slice, def_.offset);
  const uint8_t clear =
      static_cast<uint8_t>(byte & ~(def_.valueMask << def_.shift));
  instrument_.SetPatchByte(
      slice, def_.offset,
      static_cast<uint8_t>(clear | ((value & def_.valueMask) << def_.shift)));
}

void UIFM6ParamField::Draw(GUIWindow &w, int offset) {
  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  const uint8_t value = std::clamp(read(), def_.min, def_.max);
  char buffer[MAX_FIELD_WIDTH + 1];

  switch (def_.format) {
  case FM6ParamFormat::Names:
    npf_snprintf(buffer, sizeof(buffer), "%s", def_.names[value]);
    break;
  case FM6ParamFormat::Signed:
    npf_snprintf(buffer, sizeof(buffer), "%+d", value + def_.displayOffset);
    break;
  case FM6ParamFormat::Note: {
    char note[5];
    note2char(static_cast<unsigned char>(value + kBreakPointNoteOffset), note);
    note[4] = 0;
    npf_snprintf(buffer, sizeof(buffer), "%s", note);
  } break;
  case FM6ParamFormat::Numeric:
  default:
    npf_snprintf(buffer, sizeof(buffer), (def_.max >= 10) ? "%02d" : "%d",
                 value);
    break;
  }

  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);
  } else {
    DrawLabeledField(w, position, buffer, props);
  }
};

void UIFM6ParamField::ProcessArrow(unsigned short mask) {
  const int step = (mask & (EPBM_UP | EPBM_DOWN)) ? def_.coarseStep : 1;
  int delta = 0;
  if (mask & (EPBM_UP | EPBM_RIGHT)) {
    delta = step;
  } else if (mask & (EPBM_DOWN | EPBM_LEFT)) {
    delta = -step;
  }
  if (delta == 0) {
    return;
  }

  const uint8_t previous = read();
  const int value = std::clamp(previous + delta, static_cast<int>(def_.min),
                               static_cast<int>(def_.max));
  if (value == previous) {
    return;
  }
  write(static_cast<uint8_t>(value));
  SetChanged();
  NotifyObservers(
      reinterpret_cast<I_ObservableData *>(static_cast<uintptr_t>(sliceID())));
};
