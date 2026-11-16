#pragma once
#include "Application/Instruments/DecentSamplerPreset.h"
#include "Application/Instruments/I_Instrument.h"

class MSamplerInstrument : public I_Instrument {
public:
  bool LoadPreset(const char *) { return false; }
  const char *GetPresetPath() const { return ""; }
};
