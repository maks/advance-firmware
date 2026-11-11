#pragma once
#include "Application/Instruments/I_Instrument.h"

class MSamplerInstrument : public I_Instrument {
public:
  bool LoadPreset(const char *) { return false; }
};
