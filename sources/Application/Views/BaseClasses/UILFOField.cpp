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

#include "UILFOField.h"
#include "Application/AppWindow.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "ViewUtils.h"
#include <System/Console/nanoprintf.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

int arrowDelta(unsigned short mask, int smallStep, int largeStep) {
  switch (mask) {
  case EPBM_LEFT:
    return -smallStep;
  case EPBM_RIGHT:
    return smallStep;
  case EPBM_DOWN:
    return -largeStep;
  case EPBM_UP:
    return largeStep;
  default:
    break;
  }
  return 0;
}

const char *getLFOShapeDisplayName(LFOShape shape) {
  switch (shape) {
  case LFOShape::Sine:
    return char_waveform_sine1_s char_waveform_sine2_s;
  case LFOShape::Triangle:
    return char_waveform_tri_s;
  case LFOShape::Square:
    return char_waveform_pulse_s;
  case LFOShape::Ramp:
    return char_waveform_saw_s;
  case LFOShape::Random:
    return char_waveform_noise_s;
  case LFOShape::Last:
    break;
  }

  return GetLFOShapeName(shape);
}

} // namespace

UILFOField::UILFOField(const GUIPoint &position,
                       VoiceParameters &voiceParameters, size_t lfoIndex,
                       LFOFieldKind kind)
    : UIField(position), voiceParameters_(voiceParameters), lfoIndex_(lfoIndex),
      kind_(kind) {}

void UILFOField::Draw(GUIWindow &w, int offset) {
  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  const LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    char unavailable[] = "LFO n/a";
    DrawLabeledField(w, position, unavailable, props);
    return;
  }

  const LFOConfig config = settings->Get(lfoIndex_);
  char buffer[MAX_FIELD_WIDTH + 1];
  buffer[0] = '\0';

  switch (kind_) {
  case LFOFieldKind::Target:
    npf_snprintf(buffer, sizeof(buffer), "Tgt: %s",
                 GetLFOTargetInfo(config.target_).name_);
    break;
  case LFOFieldKind::Shape:
    npf_snprintf(buffer, sizeof(buffer), "Shp: %s",
                 getLFOShapeDisplayName(config.shape_));
    break;
  case LFOFieldKind::Trigger:
    npf_snprintf(buffer, sizeof(buffer), "Trg: %s",
                 GetLFOTriggerModeName(config.triggerMode_));
    break;
  case LFOFieldKind::RateMode:
    npf_snprintf(buffer, sizeof(buffer), "Typ: %s",
                 GetLFORateModeName(config.rateMode_));
    break;
  case LFOFieldKind::Rate:
    if (config.rateMode_ == LFORateMode::Sync) {
      npf_snprintf(
          buffer, sizeof(buffer), "Spd: %s",
          GetLFOSyncDivision(static_cast<size_t>(config.syncIndex_)).name_);
    } else {
      npf_snprintf(buffer, sizeof(buffer), "Spd: %02X", config.rate_);
    }
    break;
  case LFOFieldKind::Depth:
    npf_snprintf(buffer, sizeof(buffer), "Lvl: %02X", config.depth_);
    break;
  }

  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);
  } else {
    DrawLabeledField(w, position, buffer, props);
  }
}

void UILFOField::ProcessArrow(unsigned short mask) {
  switch (kind_) {
  case LFOFieldKind::Target:
    cycleTarget(arrowDelta(mask, 1, 1));
    break;
  case LFOFieldKind::Shape:
    cycleShape(arrowDelta(mask, 1, 1));
    break;
  case LFOFieldKind::Trigger:
    cycleTrigger(arrowDelta(mask, 1, 1));
    break;
  case LFOFieldKind::RateMode:
    cycleRateMode(arrowDelta(mask, 1, 1));
    break;
  case LFOFieldKind::Rate:
    adjustRate(arrowDelta(mask, 1, 10));
    break;
  case LFOFieldKind::Depth:
    adjustDepth(arrowDelta(mask, 1, 16));
    break;
  }
}

void UILFOField::ProcessClear() {
  LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return;
  }

  switch (kind_) {
  case LFOFieldKind::Target:
    settings->SetTarget(lfoIndex_, LFOTarget::None);
    break;
  case LFOFieldKind::Shape:
    settings->SetShape(lfoIndex_, LFOShape::Sine);
    break;
  case LFOFieldKind::Trigger:
    settings->SetTriggerMode(lfoIndex_, LFOTriggerMode::Trigger);
    break;
  case LFOFieldKind::RateMode:
    settings->SetRateMode(lfoIndex_, LFORateMode::Hertz);
    break;
  case LFOFieldKind::Rate:
    settings->SetRateValue(lfoIndex_, LFOSettings::DefaultRateValue);
    settings->SetSyncIndex(lfoIndex_, 4);
    break;
  case LFOFieldKind::Depth:
    settings->SetDepth(lfoIndex_, 0);
    break;
  }
}

void UILFOField::cycleTarget(int direction) {
  if (direction == 0) {
    return;
  }

  LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return;
  }
  int value = static_cast<int>(settings->Get(lfoIndex_).target_);
  const int minValue = static_cast<int>(LFOTarget::None);
  const int maxValue = static_cast<int>(LFOTarget::Last) - 1;
  const int step = direction > 0 ? 1 : -1;

  for (value += step; value >= minValue && value <= maxValue; value += step) {
    const LFOTarget target = static_cast<LFOTarget>(value);
    if (target != LFOTarget::None &&
        settings->IsTargetInUse(target, lfoIndex_)) {
      continue;
    }
    settings->SetTarget(lfoIndex_, target);
    return;
  }
}

void UILFOField::cycleShape(int direction) {
  if (direction == 0) {
    return;
  }

  LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return;
  }
  const LFOConfig config = settings->Get(lfoIndex_);
  settings->SetShape(
      lfoIndex_, static_cast<LFOShape>(std::clamp(
                     static_cast<int>(config.shape_) + (direction > 0 ? 1 : -1),
                     0, static_cast<int>(LFOShape::Last) - 1)));
}

void UILFOField::cycleTrigger(int direction) {
  if (direction == 0) {
    return;
  }

  LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return;
  }
  const LFOConfig config = settings->Get(lfoIndex_);
  settings->SetTriggerMode(
      lfoIndex_,
      static_cast<LFOTriggerMode>(std::clamp(
          static_cast<int>(config.triggerMode_) + (direction > 0 ? 1 : -1), 0,
          static_cast<int>(LFOTriggerMode::Last) - 1)));
}

void UILFOField::cycleRateMode(int direction) {
  if (direction == 0) {
    return;
  }

  LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return;
  }
  const LFOConfig config = settings->Get(lfoIndex_);
  settings->SetRateMode(
      lfoIndex_,
      static_cast<LFORateMode>(std::clamp(
          static_cast<int>(config.rateMode_) + (direction > 0 ? 1 : -1), 0,
          static_cast<int>(LFORateMode::Last) - 1)));
}

void UILFOField::adjustRate(int delta) {
  if (delta == 0) {
    return;
  }

  LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return;
  }
  const LFOConfig config = settings->Get(lfoIndex_);
  if (config.rateMode_ == LFORateMode::Sync) {
    settings->SetSyncIndex(
        lfoIndex_, static_cast<int16_t>(std::clamp(
                       config.syncIndex_ + (delta > 0 ? 1 : -1), 0,
                       static_cast<int>(GetLFOSyncDivisionCount()) - 1)));
    return;
  }

  settings->SetRateValue(lfoIndex_, std::clamp(config.rate_ + delta,
                                               LFOSettings::MinRateValue,
                                               LFOSettings::MaxRateValue));
}

void UILFOField::adjustDepth(int delta) {
  if (delta == 0) {
    return;
  }

  LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return;
  }
  const LFOConfig config = settings->Get(lfoIndex_);
  settings->SetDepth(lfoIndex_,
                     std::clamp(config.depth_ + delta, 0, depthMax()));
}

int UILFOField::depthMax() const {
  int minValue = 0;
  int maxValue = 0;
  const LFOSettings *settings = voiceParameters_.GetLFOSettings();
  if (!settings) {
    return 0;
  }
  if (!GetLFOTargetValueRange(settings->Get(lfoIndex_).target_, minValue,
                              maxValue)) {
    return 0;
  }
  return std::max(std::abs(minValue), std::abs(maxValue));
}
