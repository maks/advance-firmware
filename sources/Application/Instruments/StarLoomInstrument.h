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

#ifndef _STARLOOM_INSTRUMENT_H_
#define _STARLOOM_INSTRUMENT_H_

#include "Application/Audio/AudioSample.h"
#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Foundation/Observable.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "I_Instrument.h"
#include "VoiceParameters.h"

struct StarLoomCommandRamp {
  float current = 0.0f;
  float target = 0.0f;
  // Positive: ramping; negative: hold the override; zero: use the saved value.
  float stepsRemaining = 0.0f;
};

class StarLoomInstrument : public I_Instrument, I_Observer {

public:
  StarLoomInstrument();
  virtual ~StarLoomInstrument();
  // I_Instrument implementation
  virtual bool Init();
  virtual bool Start(int channel, unsigned char note,
                     InstrumentStartMode mode = InstrumentStartMode::NewNote);
  virtual void Stop(int channel);
  virtual bool Render(int channel, AudioSample *buffer, int size,
                      bool updateTick) override;
  void SetPitchMultiplier(int channel, float multiplier) override;
  AudioBufferLayout GetAudioBufferLayout(int channel) const override;
  virtual bool IsInitialized();
  virtual bool IsEmpty();

  virtual InstrumentType GetType() { return IT_STARLOOM; };
  bool ShouldAuditionParameterChange(FourCC parameter) const override;
  virtual void ProcessCommand(int channel, TrackerCommand cc, ushort value);
  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);
  etl::ivector<Variable *> *Variables() { return &variables_; };

  // Engine playback  start callback
  virtual void OnStart();

  // I_Observer
  virtual void Update(Observable &o, I_ObservableData *d);

protected:
private:
  static constexpr size_t BaseVariableCount = 13;
  static constexpr size_t VariableCount =
      BaseVariableCount + VoiceParameters::PersistedVariableCount;

  etl::vector<Variable *, VariableCount> variables_;

  Variable shape_;
  Variable timbre_;
  Variable color_;
  Variable signature_;
  Variable mix_;
  Variable oscillator1PitchOffset_;
  Variable oscillator2Shape_;
  Variable oscillator2Timbre_;
  Variable oscillator2Color_;
  Variable oscillator2PitchOffset_;
  Variable interactionMode_;
  Variable table_;
  Variable tableAuto_;
  TableSaveState tableState_;
};
#endif
