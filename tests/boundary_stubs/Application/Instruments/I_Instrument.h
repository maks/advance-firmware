/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#ifndef _I_INSTRUMENT_H_
#define _I_INSTRUMENT_H_

#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Externals/TinyXML2/tinyxml2.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/Variable.h"

enum InstrumentType { IT_NONE = 0, IT_SAMPLE, IT_MIDI, IT_LAST };
static const char *InstrumentTypeNames[IT_LAST] = {"NONE", "SAMPLE", "MIDI"};

class I_Instrument {
public:
  virtual ~I_Instrument() = default;

  virtual InstrumentType GetType() = 0;
  virtual bool Restore(PersistencyDocument *doc) = 0;
  virtual void Save(tinyxml2::XMLPrinter *printer) { (void)printer; }
  virtual void SetChanged() {}
  virtual void NotifyObservers() {}

  Variable *FindVariable(FourCC id) {
    for (auto *variable : variables_) {
      if (variable != nullptr && variable->GetID() == id) {
        return variable;
      }
    }
    return nullptr;
  }

protected:
  etl::vector<Variable *, 8> variables_;
};

#endif
