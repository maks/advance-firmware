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

#ifndef _INSTRUMENT_BANK_H_
#define _INSTRUMENT_BANK_H_

#include "Application/Instruments/I_Instrument.h"
#include "Application/Model/Song.h"
#include "Application/Persistency/Persistent.h"
#include "Externals/etl/include/etl/variant_pool.h"
#include "FM6Instrument.h"
#include "MSamplerInstrument.h"
#include "MidiInstrument.h"
#include "NoneInstrument.h"
#include "SIDInstrument.h"
#include "SampleInstrument.h"
#include "SampleSourceInstrument.h"
#include "StarLoomInstrument.h"
#include "VoiceParameters.h"
#include "picoSwarmInstrument.h"
#include <cstdint>

#define NO_MORE_INSTRUMENT 0x100

class InstrumentBank : public Persistent {
public:
  using SampleAssignmentList =
      etl::vector<I_Instrument *, MAX_INSTRUMENT_COUNT>;

  InstrumentBank();
  ~InstrumentBank();
  void Reset();
  void AssignDefaults();
  I_Instrument *GetInstrument(int i);
  virtual void SaveContent(tinyxml2::XMLPrinter *printer);
  virtual void RestoreContent(PersistencyDocument *doc);
  void Init();
  void OnStart();
  unsigned short GetNextAndAssignID(InstrumentType type, unsigned char id);
  void releaseInstrument(unsigned short id);
  unsigned short Clone(unsigned short i);
  unsigned short GetNextFreeInstrumentSlotId();
  VoiceParameters *GetVoiceParameters(int i);
  SampleAssignmentList CaptureSampleAssignments(int32_t sampleIndex) const;
  void RestoreSampleAssignments(const SampleAssignmentList &assignments,
                                int32_t sampleIndex) const;
  bool IsLegacySampleInstrumentSelectionEnabled() const {
    return legacySampleInstrumentSelectionEnabled_;
  }

  const etl::array<I_Instrument *, MAX_INSTRUMENT_COUNT> &
  InstrumentsList() const {
    return instruments_;
  }

private:
  void assignInstrument(int id, I_Instrument *instrument);
  void attachVoiceParameters(int id, I_Instrument *instrument);

  etl::array<I_Instrument *, MAX_INSTRUMENT_COUNT> instruments_;
  etl::array<VoiceParameters, MAX_INSTRUMENT_COUNT> voiceParameters_;
  etl::variant_pool<MAX_INSTRUMENT_COUNT, SampleInstrument,
                    SampleSourceInstrument, MidiInstrument, SIDInstrument,
                    FM6Instrument, StarLoomInstrument, picoSwarmInstrument,
                    MSamplerInstrument>
      instrumentPool_;
  NoneInstrument none_ = NoneInstrument();
  // Loading or importing a legacy sample instrument opts the project into
  // creating more of them until another project is loaded.
  bool legacySampleInstrumentSelectionEnabled_ = false;
};

#endif
