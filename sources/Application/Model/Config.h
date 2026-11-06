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

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "Application/Persistency/Persistent.h"
#include "Foundation/T_Singleton.h"
#include "Foundation/Variables/StringVariable.h"
#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "System/Console/Trace.h"

enum class AudioOutputMode : int { Analog = 0,
                                   Usb = 1,
                                   UsbAndAnalog = 2 };

class Config : public T_Singleton<Config>, public VariableContainer {
public:
  Config();
  ~Config();
  int GetValue(const char *key);
  void ProcessArguments(int argc, char **argv);
  bool Save();
  bool IsInstrumentAuditionEnabled();

  // Methods for handling color variables and themes
  void WriteColorVariables(tinyxml2::XMLPrinter *printer);
  void ReadColorVariable(PersistencyDocument *doc);

  // Theme-related methods (replacing Theme class)
  bool SaveTheme(tinyxml2::XMLPrinter *printer, const char *themeName);
  bool LoadTheme(PersistencyDocument *doc);
  bool ExportTheme(const char *themeName, bool overwrite);
  bool ImportTheme(const char *themeName);
  void ResetTheme();

private:
  etl::vector<Variable *, 28> variables_;
  // Config variables (kept as members to avoid heap allocation)
  WatchedVariable background_;
  WatchedVariable foreground_;
  WatchedVariable hiColor1_;
  WatchedVariable hiColor2_;
  WatchedVariable consoleColor_;
  WatchedVariable cursorColor_;
  WatchedVariable infoColor_;
  WatchedVariable warnColor_;
  WatchedVariable errorColor_;
  WatchedVariable accentColor_;
  WatchedVariable accentAltColor_;
  WatchedVariable emphasisColor_;
  WatchedVariable lineOut_;
  WatchedVariable midiDevice_;
  WatchedVariable midiInputDevice_;
  WatchedVariable midiControlSurface_;
  WatchedVariable midiSync_;
  WatchedVariable remoteUI_;
  WatchedVariable instrumentAudition_;
  WatchedVariable importResampler_;
  StringVariable<MAX_VARIABLE_STRING_LENGTH> themeName_;
  WatchedVariable backlightLevel_;
  WatchedVariable outputVolume_;
  WatchedVariable audioOutput_;
  WatchedVariable recordSource_;
  WatchedVariable recordLineGain_;
  WatchedVariable recordMicGain_;

  void SaveContent(tinyxml2::XMLPrinter *printer);
  void useDefaultConfig();
};

#endif
