/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _VOICE_PARAMETERS_H_
#define _VOICE_PARAMETERS_H_

#include "Application/Player/InsertEffectDefinitions.h"
#include "Externals/etl/include/etl/array.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/Variables/Variable.h"
#include "Foundation/Variables/VariableContainer.h"
#include "LFOSettings.h"
#include "MidiVoicePickup.h"

static constexpr size_t kVoiceParameterBaseVariableCount = 19;
static constexpr size_t kVoiceParameterVariableCount =
    kVoiceParameterBaseVariableCount + 6;

class VoiceParameters : public VariableContainer {
public:
  static constexpr size_t PersistedVariableCount =
      kVoiceParameterVariableCount + LFOSettings::VariableCount;
  using ValueSnapshot = etl::array<int32_t, PersistedVariableCount>;

  class InsertTypeVariable : public Variable {
  public:
    InsertTypeVariable(FourCC id, int index = -1)
        : Variable(id, nullptr, kVoiceInsertEffectCount, index) {}

    void SetAmountVariable(Variable *amount) { amount_ = amount; }
    void SetString(const char *string, bool notify = true) override;
    etl::string<MAX_VARIABLE_STRING_LENGTH> GetString() override;

  protected:
    void onChange() override {
      if (amount_) {
        amount_->SetInt(kDefaultVoiceInsertAmount, false);
      }
    }

  private:
    Variable *amount_ = nullptr;
  };

  VoiceParameters();

  void AppendTo(etl::ivector<Variable *> &variables);
  etl::ivector<Variable *> *Variables() { return &variables_; }
  void CaptureValues(ValueSnapshot &snapshot);
  bool ValuesDifferFrom(const ValueSnapshot &snapshot);
  void CopyFrom(const VoiceParameters &other);
  void Reset();
  void ApplyMidiCC(uint8_t cc, uint8_t value, MidiVoicePickup *pickup = nullptr,
                   uint8_t instrument = 0);
  LFOSettings *GetLFOSettings() { return &lfoSettings_; }
  const LFOSettings *GetLFOSettings() const { return &lfoSettings_; }

private:
  static constexpr size_t BaseVariableCount = kVoiceParameterBaseVariableCount;
  static constexpr size_t VariableCount = PersistedVariableCount;

  etl::vector<Variable *, VariableCount> variables_;
  Variable bandpassCutoff_;
  Variable bandpassResonance_;
  Variable bandpassMix_;
  Variable filterCutoff_;
  Variable filterResonance_;
  Variable filterType_;
  Variable filterMix_;
  Variable ampAttack_;
  Variable ampDecay_;
  Variable ampSustain_;
  Variable ampRelease_;
  Variable filterAttack_;
  Variable filterDecay_;
  Variable filterSustain_;
  Variable filterRelease_;
  Variable filterEnvAmount_;
  Variable amp_;
  Variable pan_;
  Variable triggerLength_;
  InsertTypeVariable insertType1_;
  Variable insertAmount1_;
  Variable insertPosition1_;
  InsertTypeVariable insertType2_;
  Variable insertAmount2_;
  Variable insertPosition2_;
  LFOSettings lfoSettings_;
};

#endif
