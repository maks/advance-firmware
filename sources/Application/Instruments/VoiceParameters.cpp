/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "VoiceParameters.h"

#include "System/Console/n_assert.h"

#include <algorithm>
#include <strings.h>

void VoiceParameters::InsertTypeVariable::SetString(const char *string,
                                                    bool notify) {
  NAssert(string);
  for (int32_t i = 0; i < kVoiceInsertEffectCount; ++i) {
    if (strcasecmp(string, kVoiceInsertEffects[i].name_) == 0 ||
        strcasecmp(string, kVoiceInsertEffects[i].displayName_) == 0) {
      Variable::SetInt(i, notify);
      return;
    }
  }

  Variable::SetInt(VAR_OFF, notify);
}

etl::string<MAX_VARIABLE_STRING_LENGTH>
VoiceParameters::InsertTypeVariable::GetString() {
  return GetVoiceInsertEffectInfo(GetInt()).displayName_;
}

VoiceParameters::VoiceParameters()
    : VariableContainer(&variables_),
      bandpassCutoff_(FourCC::VoiceBandpassCutoff, 0),
      bandpassResonance_(FourCC::VoiceBandpassResonance, 0),
      bandpassMix_(FourCC::VoiceBandpassMix, 0xFF),
      filterCutoff_(FourCC::VoiceFilterCutoff, 0xFF),
      filterResonance_(FourCC::VoiceFilterResonance, 0),
      filterType_(FourCC::VoiceFilterType, 0),
      filterMix_(FourCC::VoiceFilterMix, 0xFF),
      ampAttack_(FourCC::VoiceAmpAttack, 0),
      ampDecay_(FourCC::VoiceAmpDecay, 0),
      ampSustain_(FourCC::VoiceAmpSustain, 0xFF),
      ampRelease_(FourCC::VoiceAmpRelease, 0),
      filterAttack_(FourCC::VoiceFilterAttack, 0),
      filterDecay_(FourCC::VoiceFilterDecay, 0),
      filterSustain_(FourCC::VoiceFilterSustain, 0),
      filterRelease_(FourCC::VoiceFilterRelease, 0),
      filterEnvAmount_(FourCC::VoiceFilterEnvAmount, 0),
      amp_(FourCC::VoiceAmp, 0xFF), pan_(FourCC::VoicePan, 0x7F),
      triggerLength_(FourCC::VoiceTriggerLength, 0),
      insertType1_(GetVoiceInsertTypeFourCC(0), kDefaultVoiceInsertEffectIndex),
      insertAmount1_(GetVoiceInsertAmountFourCC(0), kDefaultVoiceInsertAmount),
      insertPosition1_(GetVoiceInsertPositionFourCC(0),
                       GetVoiceInsertPositionNames(), kVoiceInsertPositionCount,
                       kDefaultVoiceInsertPosition),
      insertType2_(GetVoiceInsertTypeFourCC(1), kDefaultVoiceInsertEffectIndex),
      insertAmount2_(GetVoiceInsertAmountFourCC(1), kDefaultVoiceInsertAmount),
      insertPosition2_(GetVoiceInsertPositionFourCC(1),
                       GetVoiceInsertPositionNames(), kVoiceInsertPositionCount,
                       kDefaultVoiceInsertPosition) {

  variables_.push_back(&bandpassCutoff_);
  variables_.push_back(&bandpassResonance_);
  variables_.push_back(&bandpassMix_);
  variables_.push_back(&filterCutoff_);
  variables_.push_back(&filterResonance_);
  variables_.push_back(&filterType_);
  variables_.push_back(&filterMix_);
  variables_.push_back(&ampAttack_);
  variables_.push_back(&ampDecay_);
  variables_.push_back(&ampSustain_);
  variables_.push_back(&ampRelease_);
  variables_.push_back(&filterAttack_);
  variables_.push_back(&filterDecay_);
  variables_.push_back(&filterSustain_);
  variables_.push_back(&filterRelease_);
  variables_.push_back(&filterEnvAmount_);
  variables_.push_back(&amp_);
  variables_.push_back(&pan_);
  variables_.push_back(&triggerLength_);
  insertType1_.SetAmountVariable(&insertAmount1_);
  variables_.push_back(&insertType1_);
  variables_.push_back(&insertAmount1_);
  variables_.push_back(&insertPosition1_);
  insertType2_.SetAmountVariable(&insertAmount2_);
  variables_.push_back(&insertType2_);
  variables_.push_back(&insertAmount2_);
  variables_.push_back(&insertPosition2_);
  lfoSettings_.AppendTo(variables_);
}

void VoiceParameters::AppendTo(etl::ivector<Variable *> &variables) {
  for (auto it = variables_.begin(); it != variables_.end(); ++it) {
    variables.insert(variables.end(), *it);
  }
}

void VoiceParameters::CaptureValues(ValueSnapshot &snapshot) {
  size_t index = 0;
  for (Variable *variable : variables_) {
    snapshot[index++] = variable->GetInt();
  }
}

bool VoiceParameters::ValuesDifferFrom(const ValueSnapshot &snapshot) {
  size_t index = 0;
  for (Variable *variable : variables_) {
    if (snapshot[index++] != variable->GetInt()) {
      return true;
    }
  }
  return false;
}

void VoiceParameters::CopyFrom(const VoiceParameters &other) {
  auto src = other.variables_.begin();
  for (auto dst = variables_.begin();
       dst != variables_.end() && src != other.variables_.end(); ++dst, ++src) {
    (*dst)->CopyFrom(**src);
  }
}

void VoiceParameters::Reset() {
  for (auto it = variables_.begin(); it != variables_.end(); ++it) {
    (*it)->Reset();
  }
}

void VoiceParameters::ApplyMidiCC(uint8_t cc, uint8_t value) {
  if (value > 127) {
    return;
  }
  struct Mapping {
    uint8_t cc;
    FourCC field;
    int32_t min;
    int32_t max;
  };
  const Mapping mappings[] = {
      {7, FourCC::VoiceAmp, 0, 255},
      {10, FourCC::VoicePan, 0, 255},
      {14, FourCC::VoiceTriggerLength, 0, 96},
      {15, FourCC::VoiceBandpassCutoff, 0, 255},
      {16, FourCC::VoiceBandpassMix, 0, 255},
      {17, FourCC::VoiceFilterType, 0, 255},
      {18, FourCC::VoiceFilterEnvAmount, -64, 64},
      {19, FourCC::VoiceFilterAttack, 0, 255},
      {20, FourCC::VoiceFilterDecay, 0, 255},
      {21, FourCC::VoiceFilterSustain, 0, 255},
      {22, FourCC::VoiceFilterRelease, 0, 255},
      {23, FourCC::VoiceAmpSustain, 0, 255},
      {24, GetVoiceInsertTypeFourCC(0), 0, kVoiceInsertEffectCount - 1},
      {25, GetVoiceInsertAmountFourCC(0), 0, 255},
      {26, GetVoiceInsertPositionFourCC(0), 0, kVoiceInsertPositionCount - 1},
      {27, GetVoiceInsertTypeFourCC(1), 0, kVoiceInsertEffectCount - 1},
      {28, GetVoiceInsertAmountFourCC(1), 0, 255},
      {29, GetVoiceInsertPositionFourCC(1), 0, kVoiceInsertPositionCount - 1},
      {71, FourCC::VoiceFilterResonance, 0, 255},
      {72, FourCC::VoiceAmpRelease, 0, 255},
      {73, FourCC::VoiceAmpAttack, 0, 255},
      {74, FourCC::VoiceFilterCutoff, 0, 255},
      {75, FourCC::VoiceAmpDecay, 0, 255},
  };
  const auto scale = [value](int32_t min, int32_t max) {
    return min + (static_cast<int32_t>(value) * (max - min) + 63) / 127;
  };
  for (const Mapping &mapping : mappings) {
    if (mapping.cc == cc) {
      Variable *variable = FindVariable(mapping.field);
      if (variable) {
        const int32_t scaled =
            mapping.min == -64 && mapping.max == 64
                ? (value < 64 ? static_cast<int32_t>(value) - 64
                              : (static_cast<int32_t>(value) - 64) * 64 / 63)
                : scale(mapping.min, mapping.max);
        variable->SetInt(scaled);
      }
      return;
    }
  }
  if (cc < 30 || cc > 41) {
    return;
  }
  LFOSettings *lfo = GetLFOSettings();
  const size_t slot = (cc - 30) / 6;
  const uint8_t field = (cc - 30) % 6;
  const LFOConfig config = lfo->Get(slot);
  switch (field) {
  case 0:
    lfo->SetTarget(slot, static_cast<LFOTarget>(scale(
                             0, static_cast<int32_t>(LFOTarget::Last) - 1)));
    break;
  case 1:
    lfo->SetShape(slot, static_cast<LFOShape>(scale(
                            0, static_cast<int32_t>(LFOShape::Last) - 1)));
    break;
  case 2:
    lfo->SetTriggerMode(slot, static_cast<LFOTriggerMode>(scale(0, 1)));
    break;
  case 3:
    lfo->SetRateMode(slot, static_cast<LFORateMode>(scale(0, 1)));
    break;
  case 4:
    if (config.rateMode_ == LFORateMode::Sync) {
      lfo->SetSyncIndex(
          slot, scale(0, static_cast<int32_t>(GetLFOSyncDivisionCount()) - 1));
    } else {
      lfo->SetRateValue(slot, scale(0, 255));
    }
    break;
  case 5: {
    int min = 0;
    int max = 0;
    if (GetLFOTargetValueRange(config.target_, min, max)) {
      lfo->SetDepth(slot, scale(0, std::max(-min, max)));
    }
    break;
  }
  }
}
