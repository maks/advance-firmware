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

#include "InstrumentBank.h"
#include "Application/Instruments/FM6Instrument.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Instruments/StarLoomInstrument.h"
#include "Application/Instruments/picoSwarmInstrument.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Utils/char.h"
#include "Filters.h"
#include "MidiInstrument.h"
#include "System/io/Status.h"

#define XML_DEBUG_LOGGING 0

// The shared VoiceParameters default an infinite amp release to 0x00. SID and
// FM6 instruments instead want their internal/DX7 envelopes to control the
// full audible release, so they start with an "infinite" amp release. This is
// their clean default state (not a user edit), so it is applied as the
// variable's default too and restored to the shared canonical value when the
// instrument is released back to the pool.
static constexpr uint8_t canonicalVoiceAmpRelease = 0x00;
static constexpr uint8_t infiniteVoiceAmpRelease = 0xFF;

// Contain all instrument definition
InstrumentBank::InstrumentBank()
    : Persistent("INSTRUMENTBANK"), instrumentPool_() {
  for (size_t i = 0; i < instruments_.max_size(); i++) {
    instruments_[i] = &none_;
  }

  Status::Set("All instruments preloaded");
};

InstrumentBank::~InstrumentBank() { Reset(); };

void InstrumentBank::Reset() {
  for (size_t i = 0; i < instruments_.max_size(); i++) {
    if (instruments_[i] != &none_) {
      releaseInstrument(i);
    } else {
      voiceParameters_[i].Reset();
    }
  }
  legacySampleInstrumentSelectionEnabled_ = false;
};

I_Instrument *InstrumentBank::GetInstrument(int i) { return instruments_[i]; };

VoiceParameters *InstrumentBank::GetVoiceParameters(int i) {
  if (i < 0 || i >= MAX_INSTRUMENT_COUNT) {
    return nullptr;
  }
  return &voiceParameters_[i];
}

InstrumentBank::SampleAssignmentList
InstrumentBank::CaptureSampleAssignments(int32_t sampleIndex) const {
  SampleAssignmentList assignments;
  for (I_Instrument *instrument : instruments_) {
    if (instrument && instrument->GetType() == IT_SAMPLE &&
        static_cast<SampleInstrument *>(instrument)->GetSampleIndex() ==
            sampleIndex) {
      assignments.push_back(instrument);
    } else if (instrument && instrument->GetType() == IT_SAMPLE_SOURCE &&
               static_cast<SampleSourceInstrument *>(instrument)
                       ->GetSampleIndex() == sampleIndex) {
      assignments.push_back(instrument);
    }
  }
  return assignments;
}

void InstrumentBank::RestoreSampleAssignments(
    const SampleAssignmentList &assignments, int32_t sampleIndex) const {
  for (I_Instrument *instrument : assignments) {
    if (instrument->GetType() == IT_SAMPLE) {
      static_cast<SampleInstrument *>(instrument)->AssignSample(sampleIndex);
    } else if (instrument->GetType() == IT_SAMPLE_SOURCE) {
      static_cast<SampleSourceInstrument *>(instrument)
          ->AssignSample(sampleIndex);
    }
  }
}

void InstrumentBank::assignInstrument(int id, I_Instrument *instrument) {
  if (InstrumentTypeUsesModernAudioVoice(instrument->GetType())) {
    attachVoiceParameters(id, instrument);
  }
  instruments_[id] = instrument;
}

void InstrumentBank::attachVoiceParameters(int id, I_Instrument *instrument) {
  if (!instrument || instrument == &none_) {
    return;
  }

  voiceParameters_[id].Reset();
  voiceParameters_[id].AppendTo(*instrument->Variables());
}

void InstrumentBank::SaveContent(tinyxml2::XMLPrinter *printer) {
  char hex[3];
  int i = 0;
  for (auto &instr : instruments_) {
    if (!instr->IsEmpty()) {
      hex2char(i, hex);
      printer->OpenElement("INSTRUMENT");
      printer->PushAttribute("ID", hex);

      // Let the instrument save its own content
      instr->SaveContent(printer);

      printer->CloseElement(); // INSTRUMENT
    }
    i++;
  }
};

void InstrumentBank::RestoreContent(PersistencyDocument *doc) {

  bool elem = doc->FirstChild();
  while (elem) {
    // Check it is an instrument
    if (!strcasecmp(doc->ElemName(), "INSTRUMENT")) {
      // Get the instrument ID
      unsigned char id = '\0';
      char instype[16];
      instype[0] = '\0';
      bool hasId = false;
      bool hasType = false;
      bool hasAttr = doc->NextAttribute();
      while (hasAttr) {
        if (!strcasecmp(doc->attrname_, "ID")) {
          unsigned char b1 = (c2h__(doc->attrval_[0])) << 4;
          unsigned char b2 = c2h__(doc->attrval_[1]);
          id = b1 + b2;
          hasId = true;
#if XML_DEBUG_LOGGING
          Trace::Log("INSTRUMENTBANK", "instrument ID from xml:%d", id);
#endif
        }
        if (!strcasecmp(doc->attrname_, "TYPE")) {
          strncpy(instype, doc->attrval_, sizeof(instype) - 1);
          instype[sizeof(instype) - 1] = '\0';
          hasType = true;
#if XML_DEBUG_LOGGING
          Trace::Log("INSTRUMENTBANK", "instrument type from xml:%s", instype);
#endif
        }
        if (hasId && hasType) {
          break;
        }
        hasAttr = doc->NextAttribute();
      }

      InstrumentType instrType = IT_SAMPLE; // default if no type in project XML
      if (instype[0] != '\0') {
        for (uint i = 0; i < IT_LAST; i++) {
          if (!strcasecmp(instype, InstrumentTypeNames[i])) {
            instrType = (InstrumentType)i;
            break;
          }
        }
      }
      if (id < MAX_INSTRUMENT_COUNT) {
        if (GetNextAndAssignID(instrType, id) == NO_MORE_INSTRUMENT) {
          Trace::Error("Failed to allocate instrument type:%d", instrType);
          // TODO: need to show user error message that proj file is invalid
        }
        I_Instrument *instr = instruments_[id];

        // Let the instrument restore its own content
        instr->RestoreContent(doc);
      }
    }
    elem = doc->NextSibling();
  };
};

void InstrumentBank::Init() {}

// Get the next available instance of the given Instrument type from the pool of
// unused Instruments and assign it to the given instrument "slot id"
unsigned short InstrumentBank::GetNextAndAssignID(InstrumentType type,
                                                  uint8_t id) {
  switch (type) {
  case IT_SAMPLE: {
    SampleInstrument *si = instrumentPool_.create<SampleInstrument>();
    if (si == nullptr) {
      Trace::Log("INSTRUMENTBANK", "Sample INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    si->Init();

    Variable *sample = si->FindVariable(FourCC::SampleInstrumentSample);
    if (sample) {
      if (sample->GetInt() == -1) {
        assignInstrument(id, si);
        legacySampleInstrumentSelectionEnabled_ = true;
      } else {
        Trace::Log("INSTRUMENTBANK",
                   "unexpected sample value for new instrument: %d",
                   sample->GetInt());
      }
    }
    return id;
  } break;
  case IT_SAMPLE_SOURCE: {
    SampleSourceInstrument *ssi =
        instrumentPool_.create<SampleSourceInstrument>();
    if (ssi == nullptr) {
      Trace::Log("INSTRUMENTBANK", "Sample Source INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    ssi->Init();

    Variable *sample = ssi->FindVariable(FourCC::SampleSourceInstrumentSample);
    if (sample) {
      if (sample->GetInt() == -1) {
        assignInstrument(id, ssi);
      } else {
        Trace::Log("INSTRUMENTBANK",
                   "unexpected sample source value for new instrument: %d",
                   sample->GetInt());
      }
    }
    return id;
  } break;
  case IT_MIDI: {
    MidiInstrument *mi = instrumentPool_.create<MidiInstrument>();
    if (mi == nullptr) {
      Trace::Error("MIDI INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    mi->Init();
    assignInstrument(id, mi);
    return id;
  } break;
  case IT_SID: {
    SIDInstrument *si = instrumentPool_.create<SIDInstrument>();
    if (si == nullptr) {
      Trace::Error("SID INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    si->Init();
    assignInstrument(id, si);
    // Start new SID instruments with an infinite Amp release so their internal
    // ADSRs can control the complete audible release. See the note on
    // infiniteVoiceAmpRelease: this is the instrument's default state, so it
    // is adopted as the variable's default too.
    Variable *ampRelease = si->FindVariable(FourCC::VoiceAmpRelease);
    if (ampRelease) {
      ampRelease->SetIntAsDefault(infiniteVoiceAmpRelease);
    }
    return id;
  } break;
  case IT_FM6: {
    FM6Instrument *fm6 = instrumentPool_.create<FM6Instrument>();
    if (fm6 == nullptr) {
      Trace::Error("FM6 INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    fm6->Init();
    assignInstrument(id, fm6);
    // Start new FM6 instruments with an infinite Amp release so the DX7 patch
    // envelopes can control the complete audible release. See the note on
    // infiniteVoiceAmpRelease: this is the instrument's default state, so it
    // is adopted as the variable's default too.
    Variable *ampRelease = fm6->FindVariable(FourCC::VoiceAmpRelease);
    if (ampRelease) {
      ampRelease->SetIntAsDefault(infiniteVoiceAmpRelease);
    }
    return id;
  } break;
  case IT_STARLOOM: {
    StarLoomInstrument *starLoom = instrumentPool_.create<StarLoomInstrument>();
    if (starLoom == nullptr) {
      Trace::Error("STARLOOM INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    starLoom->Init();
    assignInstrument(id, starLoom);
    return id;
  } break;
  case IT_PICO_SWARM: {
    picoSwarmInstrument *picoSwarm =
        instrumentPool_.create<picoSwarmInstrument>();
    if (picoSwarm == nullptr) {
      Trace::Error("PICOSWARM INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    picoSwarm->Init();
    assignInstrument(id, picoSwarm);
    return id;
  } break;
  case IT_MSAMPLER: {
    MSamplerInstrument *msampler = instrumentPool_.create<MSamplerInstrument>();
    if (msampler == nullptr) {
      Trace::Error("MSAMPLER INSTRUMENT EXHAUSTED!");
      return NO_MORE_INSTRUMENT;
    }
    msampler->Init();
    assignInstrument(id, msampler);
    return id;
  } break;
  case IT_NONE:
    assignInstrument(id, &none_);
    return id;
  default:
    break;
  }

  return NO_MORE_INSTRUMENT;
};

void InstrumentBank::releaseInstrument(unsigned short id) {
  auto instrument = instruments_[id];

  switch (instrument->GetType()) {
  case IT_SAMPLE:
    instrumentPool_.destroy(static_cast<SampleInstrument *>(instrument));
    break;
  case IT_SAMPLE_SOURCE:
    instrumentPool_.destroy(static_cast<SampleSourceInstrument *>(instrument));
    break;
  case IT_MIDI:
    instrumentPool_.destroy(static_cast<MidiInstrument *>(instrument));
    break;
  case IT_SID:
    instrumentPool_.destroy(static_cast<SIDInstrument *>(instrument));
    break;
  case IT_FM6:
    instrumentPool_.destroy(static_cast<FM6Instrument *>(instrument));
    break;
  case IT_STARLOOM:
    instrumentPool_.destroy(static_cast<StarLoomInstrument *>(instrument));
    break;
  case IT_PICO_SWARM:
    instrumentPool_.destroy(static_cast<picoSwarmInstrument *>(instrument));
    break;
  case IT_MSAMPLER:
    instrumentPool_.destroy(static_cast<MSamplerInstrument *>(instrument));
    break;
  case IT_NONE:
    // NA: None is a "singleton" so no need to release from pool
    // BUT it can be assigned to any number of slots
  default:
    break;
  }
  instruments_[id] = &none_;
  // SID/FM6 instruments override the amp release default (see
  // infiniteVoiceAmpRelease). Restore the shared canonical default before
  // resetting, otherwise the override would leak to the next instrument
  // assigned to this slot (and make a fresh instrument look pre-modified).
  Variable *ampRelease =
      voiceParameters_[id].FindVariable(FourCC::VoiceAmpRelease);
  if (ampRelease) {
    ampRelease->SetIntAsDefault(canonicalVoiceAmpRelease);
  }
  voiceParameters_[id].Reset();
}

unsigned short InstrumentBank::GetNextFreeInstrumentSlotId() {
  for (unsigned short i = 0; i < instruments_.max_size(); i++) {
    if (instruments_[i] == &none_) {
      return i;
    }
  }
  return NO_MORE_INSTRUMENT;
}

unsigned short InstrumentBank::Clone(unsigned short i) {
  I_Instrument *src = instruments_[i];

  // Find next available instrument slot
  auto nextFreeInstrumentSlotId = GetNextFreeInstrumentSlotId();

  unsigned short next =
      GetNextAndAssignID(src->GetType(), nextFreeInstrumentSlotId);

  if (next == NO_MORE_INSTRUMENT) {
    return NO_MORE_INSTRUMENT;
  }

  I_Instrument *dst = instruments_[next];

  // sanity check not trying to clone into itself
  if (src == dst) {
    return NO_MORE_INSTRUMENT;
  }

  dst->CloneFrom(*src);
  return next;
}

void InstrumentBank::OnStart() {
  for (auto &elem : instruments_) {
    elem->OnStart();
  }
  init_filters();
};
