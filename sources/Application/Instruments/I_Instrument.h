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

#ifndef _I_INSTRUMENT_H_
#define _I_INSTRUMENT_H_

#include "Application/Audio/AudioBufferLayout.h"
#include "Application/Audio/AudioSample.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/Persistent.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/stringutils.h"
#include "Externals/etl/include/etl/string.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/VariableContainer.h"
#include <cstdint>

// NewNote resets command state; TiedNote continues playback and commands;
// Retrigger (IRT) restarts audio playback while retaining command state.
// MIDI treats Retrigger as NewNote, preserving its existing MIDI semantics.
enum class InstrumentStartMode { NewNote, TiedNote, Retrigger };

enum InstrumentType {
  IT_NONE = 0,
  IT_SAMPLE,
  IT_SAMPLE_SOURCE,
  IT_MIDI,
  IT_SID,
  IT_FM6,
  IT_STARLOOM,
  IT_PICO_SWARM,
  IT_LAST
};
static const char *InstrumentTypeNames[IT_LAST] = {
    "NONE", "SAMPLE", "SAMPLESOURCE", "MIDI",
    "SID",  "FM6",    "STARLOOM",     "PICOSWARM"};

inline bool InstrumentTypeUsesModernAudioVoice(InstrumentType type) {
  // This is the single source of truth for shared VoiceParameters and common
  // AudioVoice command handling. Legacy Sample and MIDI remain self-contained.
  switch (type) {
  case IT_SAMPLE_SOURCE:
  case IT_SID:
  case IT_FM6:
  case IT_STARLOOM:
  case IT_PICO_SWARM:
    return true;
  default:
    return false;
  }
}

class I_Instrument : public VariableContainer,
                     public Observable,
                     public Persistent {
protected:
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name_;

public:
  I_Instrument(etl::ivector<Variable *> *list,
               const char *nodeName = "INSTRUMENT",
               bool registerWithPersistence = false)
      : VariableContainer(list),
        Persistent(nodeName, registerWithPersistence){};
  virtual ~I_Instrument();

  // Initialisation routine

  virtual bool Init() = 0;

  // Start & stop the instument
  virtual bool
  Start(int channel, unsigned char note,
        InstrumentStartMode mode = InstrumentStartMode::NewNote) = 0;
  virtual bool StartWithVelocity(
      int channel, unsigned char note, uint8_t velocity,
      InstrumentStartMode mode = InstrumentStartMode::NewNote) {
    (void)velocity;
    return Start(channel, note, mode);
  }
  virtual void Release(int channel) { (void)channel; }
  virtual void Stop(int channel) = 0;

  // Engine playback  start callback

  virtual void OnStart() = 0;

  virtual bool IsInitialized() = 0;

  virtual bool IsEmpty() = 0;

  virtual InstrumentType GetType() = 0;

  // Instrument editors use this to decide whether changing a parameter should
  // retrigger the instrument audition note. Instruments opt in only the
  // parameters whose value directly changes the sound.
  virtual bool ShouldAuditionParameterChange(FourCC parameter) const {
    (void)parameter;
    return false;
  }

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetDefaultName() {
    return etl::string<MAX_INSTRUMENT_NAME_LENGTH>(
        InstrumentTypeNames[GetType()]);
  };

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetUserSetName() {
    return name_;
  };

  // Set the instrument name
  virtual void SetName(const char *name) {
    name_ = name;
    SetChanged();
    NotifyObservers();
  };

  // Set the instrument name from a Variable
  virtual void SetNameFromVariable(Variable *nameVar) {
    if (nameVar) {
      name_ = nameVar->GetString();
      SetChanged();
      NotifyObservers();
    }
  };

  // return the name to display in the UI
  // will be the user set name if available other the default name is returned
  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetDisplayName() {
    auto name = GetUserSetName();
    if (!name.empty()) {
      return name;
    }
    return GetDefaultName();
  }

  virtual void ProcessCommand(int channel, TrackerCommand cc, ushort value) = 0;

  virtual bool Render(int channel, AudioSample *buffer, int size,
                      bool updateTick) {
    (void)channel;
    (void)buffer;
    (void)size;
    (void)updateTick;
    return false;
  }

  // AudioVoice owns common pitch commands. Modern audio sources translate the
  // resulting multiplier into their native playback-frequency control.
  virtual void SetPitchMultiplier(int channel, float multiplier) {
    (void)channel;
    (void)multiplier;
  }

  virtual AudioBufferLayout GetAudioBufferLayout(int channel) const {
    (void)channel;
    return AudioBufferLayout::StereoInterleaved;
  }

  virtual bool GetPlaybackPosition(int channel, uint32_t &position) const {
    (void)channel;
    position = 0;
    return false;
  }

  virtual void Purge();
  virtual void CloneFrom(I_Instrument &src);

  virtual int GetTable() = 0;
  virtual bool GetTableAutomation() = 0;

  virtual void GetTableState(TableSaveState &state) = 0;
  virtual void SetTableState(TableSaveState &state) = 0;
  virtual etl::ivector<Variable *> *Variables() = 0;

  // Persistent implementation
  virtual void SaveContent(tinyxml2::XMLPrinter *printer) override;
  virtual void RestoreContent(PersistencyDocument *doc) override;
};
#endif
