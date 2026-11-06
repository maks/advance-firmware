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

#include "Config.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Externals/etl/include/etl/flat_map.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/string_utilities.h"
#include "System/Console/Trace.h"
#include "System/Console/nanoprintf.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include "System/FileSystem/PathUtils.h"
#include "ThemeConstants.h"
#include "Variable.h"
#include <stdlib.h>

namespace {

constexpr FourCC::enum_type THEME_COLOR_VARIABLE_IDS[] = {
    FourCC::VarBGColor,
    FourCC::VarFGColor,
    FourCC::VarHI1Color,
    FourCC::VarHI2Color,
    FourCC::VarConsoleColor,
    FourCC::VarCursorColor,
    FourCC::VarInfoColor,
    FourCC::VarWarnColor,
    FourCC::VarErrorColor,
    FourCC::VarAccentColor,
    FourCC::VarAccentAltColor,
    FourCC::VarEmphasisColor,
};

bool isThemeColorVariable(FourCC id) {
  for (FourCC::enum_type colorID : THEME_COLOR_VARIABLE_IDS) {
    if (id == colorID) {
      return true;
    }
  }
  return false;
}

bool resolveThemePath(const char *themeName, FileSystemPath::PathString &path) {
  if (themeName == nullptr || themeName[0] == '\0') {
    return false;
  }

  if (FileSystemPath::IsAbsolutePath(themeName)) {
    return FileSystemPath::NormalizePath(themeName, path);
  }

  FileSystemPath::PathString themeLeaf(themeName);
  const char *extension = strrchr(themeName, '.');
  if (!extension || strcmp(extension, THEME_FILE_EXTENSION) != 0) {
    themeLeaf.append(THEME_FILE_EXTENSION);
  }

  return FileSystemPath::JoinPath(THEMES_DIR, themeLeaf.c_str(), path);
}

bool extractThemeName(const char *themePath,
                      etl::string<MAX_THEME_NAME_LENGTH> &themeName) {
  FileSystemPath::PathString baseName;
  if (!FileSystemPath::BaseName(themePath, baseName)) {
    themeName.clear();
    return false;
  }

  const char *extension = strrchr(baseName.c_str(), '.');
  if (extension && strcmp(extension, THEME_FILE_EXTENSION) == 0) {
    themeName.assign(baseName.c_str(),
                     static_cast<size_t>(extension - baseName.c_str()));
  } else {
    themeName = baseName.c_str();
  }

  return !themeName.empty();
}

} // namespace

#define CONFIG_FILE_PATH "/.config.xml"
#define CONFIG_VERSION_NUMBER 1

#define MIDI_DEVICE_LEN 4

static const char *lineOutOptions[3] = {"HP Low", "HP High", "Line Level"};
static const char *midiDeviceList[MIDI_DEVICE_LEN] = {"Off", "TRS", "USB",
                                                      "TRS+USB"};
static const char *midiInputDeviceList[4] = {"Off", "TRS", "USB",
                                             "TRS+USB"};
static const char *midiSendSync[2] = {"Off", "Send"};
static const char *midiClockSyncOptions[2] = {"Internal", "External"};
static const char *remoteUIOnOff[2] = {"Off", "On"};
static const char *instrumentAuditionOnOff[2] = {"Off", "On"};
static const char *midiControlSurfaceOptions[2] = {"Off", "Auto"};
static const char *importResamplerOptions[] = {"None", "Linear", "Sinc",
                                               "Sinc HQ"};
static const char *audioOutputOptions[3] = {"Analog", "USB", "USB+Analog"};
static constexpr int kImportResamplerOptionCount = 4;

// NOTE: these MUST match up to the RecordSource enum in record.h (of all
// adapters) also note we *dont* show "All Off" as a UI option for now
static const char *recordSourceOptions[4] = {"All Off", "Line In", "Mic",
                                             "USB In"};

// Param keys MUST fit in this length limit!
typedef etl::string<13> ParamString;

// Use default color values from ThemeConstants.h
// Other default values not related to theme colors:
constexpr int DEFAULT_LINEOUT = 0x2;
constexpr int DEFAULT_MIDIDEVICE = 0x0;
constexpr int DEFAULT_MIDI_INPUT_DEVICE = 0x0;
constexpr int DEFAULT_MIDI_CONTROL_SURFACE = 0x0;
constexpr int DEFAULT_MIDISYNC = 0x0;
constexpr int DEFAULT_REMOTEUI = 0x1;
constexpr int DEFAULT_INSTRUMENT_AUDITION = 0x1;
constexpr int DEFAULT_BACKLIGHT_LEVEL = 0xFF; // Default to max brightness (255)
constexpr int DEFAULT_REC_SOURCE = 0x0;
constexpr int DEFAULT_RECORD_LINE_GAIN_DB = 0;
constexpr int DEFAULT_RECORD_MIC_GAIN_DB = 0;
constexpr int DEFAULT_OUTPUT_VOLUME = 40;
constexpr int DEFAULT_AUDIO_OUTPUT = 0;
constexpr int DEFAULT_IMPORT_RESAMPLER = 2; // Sinc by default

int NormalizeImportResamplerValue(int value) {
  if (value <= 0 || value >= kImportResamplerOptionCount) {
    return DEFAULT_IMPORT_RESAMPLER;
  }

  return value;
}

int NormalizeMidiControlSurfaceValue(int value) {
  return value <= 0 ? 0 : 1;
}

// Use a struct to define parameter information
struct ConfigParam {
  const char *name;
  union {
    int intValue;
    const char *strValue;
  } defaultValue;
  FourCC::enum_type fourcc;
  const char **options;
  int optionCount;
  bool isString;
};

// Define parameters as a static array instead of a ETL flat_map for example,
// because using a flat_map static requires too much stack space for
// initialization
static const ConfigParam configParams[] = {
    // Color variables
    {"BACKGROUND",
     {.intValue = ThemeConstants::DEFAULT_BACKGROUND},
     FourCC::VarBGColor,
     nullptr,
     0,
     false},
    {"FOREGROUND",
     {.intValue = ThemeConstants::DEFAULT_FOREGROUND},
     FourCC::VarFGColor,
     nullptr,
     0,
     false},
    {"HICOLOR1",
     {.intValue = ThemeConstants::DEFAULT_HICOLOR1},
     FourCC::VarHI1Color,
     nullptr,
     0,
     false},
    {"HICOLOR2",
     {.intValue = ThemeConstants::DEFAULT_HICOLOR2},
     FourCC::VarHI2Color,
     nullptr,
     0,
     false},
    {"CONSOLECOLOR",
     {.intValue = ThemeConstants::DEFAULT_CONSOLECOLOR},
     FourCC::VarConsoleColor,
     nullptr,
     0,
     false},
    {"CURSORCOLOR",
     {.intValue = ThemeConstants::DEFAULT_CURSORCOLOR},
     FourCC::VarCursorColor,
     nullptr,
     0,
     false},
    {"INFOCOLOR",
     {.intValue = ThemeConstants::DEFAULT_INFOCOLOR},
     FourCC::VarInfoColor,
     nullptr,
     0,
     false},
    {"WARNCOLOR",
     {.intValue = ThemeConstants::DEFAULT_WARNCOLOR},
     FourCC::VarWarnColor,
     nullptr,
     0,
     false},
    {"ERRORCOLOR",
     {.intValue = ThemeConstants::DEFAULT_ERRORCOLOR},
     FourCC::VarErrorColor,
     nullptr,
     0,
     false},
    {"ACCENTCOLOR",
     {.intValue = ThemeConstants::DEFAULT_ACCENT},
     FourCC::VarAccentColor,
     nullptr,
     0,
     false},
    {"ACCENTALTCOLOR",
     {.intValue = ThemeConstants::DEFAULT_ACCENT_ALT},
     FourCC::VarAccentAltColor,
     nullptr,
     0,
     false},
    {"EMPHASISCOLOR",
     {.intValue = ThemeConstants::DEFAULT_EMPHASIS},
     FourCC::VarEmphasisColor,
     nullptr,
     0,
     false},

    // Device settings with options
    {"LINEOUT",
     {.intValue = DEFAULT_LINEOUT},
     FourCC::VarLineOut,
     lineOutOptions,
     3,
     false},
    {"MIDIDEVICE",
     {.intValue = DEFAULT_MIDIDEVICE},
     FourCC::VarMidiDevice,
     midiDeviceList,
     4,
     false},
    {"MIDIINPUTDEV",
     {.intValue = DEFAULT_MIDI_INPUT_DEVICE},
     FourCC::VarMidiInputDevice,
     midiInputDeviceList,
     4,
     false},
    {"MIDICTRLSURF",
     {.intValue = DEFAULT_MIDI_CONTROL_SURFACE},
     FourCC::VarMidiControlSurface,
     midiControlSurfaceOptions,
     2,
     false},
    {"MIDISYNC",
     {.intValue = DEFAULT_MIDISYNC},
     FourCC::VarMidiSync,
     midiSendSync,
     2,
     false},
    {"REMOTEUI",
     {.intValue = DEFAULT_REMOTEUI},
     FourCC::VarRemoteUI,
     remoteUIOnOff,
     2,
     false},
    {"INSTAUDITION",
     {.intValue = DEFAULT_INSTRUMENT_AUDITION},
     FourCC::VarInstrumentAudition,
     instrumentAuditionOnOff,
     2,
     false},
    // {"RESERVED1", ThemeConstants::DEFAULT_RESERVED1,
    // FourCC::VarReserved1Color},
    // {"RESERVED2", ThemeConstants::DEFAULT_RESERVED2,
    // FourCC::VarReserved2Color},
    // {"RESERVED3", ThemeConstants::DEFAULT_RESERVED3,
    // FourCC::VarReserved3Color},
    // {"RESERVED4", ThemeConstants::DEFAULT_RESERVED4,
    // FourCC::VarReserved4Color},

    {"THEMENAME",
     {.strValue = ThemeConstants::DEFAULT_THEME_NAME},
     FourCC::VarThemeName,
     nullptr,
     0,
     true},

    // Display brightness setting
    {"BACKLIGHTLEVEL",
     {.intValue = DEFAULT_BACKLIGHT_LEVEL},
     FourCC::VarBacklightLevel,
     nullptr,
     0,
     false},
    {"OUTPUTVOLUME",
     {.intValue = DEFAULT_OUTPUT_VOLUME},
     FourCC::VarOutputVolume,
     nullptr,
     0,
     false},
    {"AUDIOOUTPUT",
     {.intValue = DEFAULT_AUDIO_OUTPUT},
     FourCC::VarAudioOutput,
     audioOutputOptions,
     3,
     false},
    {"IMPORTRESAMP",
     {.intValue = DEFAULT_IMPORT_RESAMPLER},
     FourCC::VarImportResampler,
     importResamplerOptions,
     kImportResamplerOptionCount,
     false},

    {"RECORDSOURCE",
     {.intValue = 1},
     FourCC::VarRecordSource,
     recordSourceOptions,
     4,
     false},
    {"RECORDLINEGAIN",
     {.intValue = DEFAULT_RECORD_LINE_GAIN_DB},
     FourCC::VarRecordLineGain,
     nullptr,
     0,
     false},
    {"RECORDMICGAIN",
     {.intValue = DEFAULT_RECORD_MIC_GAIN_DB},
     FourCC::VarRecordMicGain,
     nullptr,
     0,
     false},
};

Config::Config()
    : VariableContainer(&variables_),
      background_(FourCC::VarBGColor,
                  static_cast<int>(ThemeConstants::DEFAULT_BACKGROUND)),
      foreground_(FourCC::VarFGColor,
                  static_cast<int>(ThemeConstants::DEFAULT_FOREGROUND)),
      hiColor1_(FourCC::VarHI1Color,
                static_cast<int>(ThemeConstants::DEFAULT_HICOLOR1)),
      hiColor2_(FourCC::VarHI2Color,
                static_cast<int>(ThemeConstants::DEFAULT_HICOLOR2)),
      consoleColor_(FourCC::VarConsoleColor,
                    static_cast<int>(ThemeConstants::DEFAULT_CONSOLECOLOR)),
      cursorColor_(FourCC::VarCursorColor,
                   static_cast<int>(ThemeConstants::DEFAULT_CURSORCOLOR)),
      infoColor_(FourCC::VarInfoColor,
                 static_cast<int>(ThemeConstants::DEFAULT_INFOCOLOR)),
      warnColor_(FourCC::VarWarnColor,
                 static_cast<int>(ThemeConstants::DEFAULT_WARNCOLOR)),
      errorColor_(FourCC::VarErrorColor,
                  static_cast<int>(ThemeConstants::DEFAULT_ERRORCOLOR)),
      accentColor_(FourCC::VarAccentColor,
                   static_cast<int>(ThemeConstants::DEFAULT_ACCENT)),
      accentAltColor_(FourCC::VarAccentAltColor,
                      static_cast<int>(ThemeConstants::DEFAULT_ACCENT_ALT)),
      emphasisColor_(FourCC::VarEmphasisColor,
                     static_cast<int>(ThemeConstants::DEFAULT_EMPHASIS)),
      lineOut_(FourCC::VarLineOut, lineOutOptions, 3, DEFAULT_LINEOUT),
      midiDevice_(FourCC::VarMidiDevice, midiDeviceList, 4, DEFAULT_MIDIDEVICE),
      midiInputDevice_(FourCC::VarMidiInputDevice, midiInputDeviceList,
                       4, DEFAULT_MIDI_INPUT_DEVICE),
      midiControlSurface_(FourCC::VarMidiControlSurface,
                          midiControlSurfaceOptions, 2,
                          DEFAULT_MIDI_CONTROL_SURFACE),
      midiSync_(FourCC::VarMidiSync, midiSendSync, 2, DEFAULT_MIDISYNC),
      remoteUI_(FourCC::VarRemoteUI, remoteUIOnOff, 2, DEFAULT_REMOTEUI),
      instrumentAudition_(FourCC::VarInstrumentAudition,
                          instrumentAuditionOnOff, 2,
                          DEFAULT_INSTRUMENT_AUDITION),
      importResampler_(FourCC::VarImportResampler, importResamplerOptions,
                       kImportResamplerOptionCount, DEFAULT_IMPORT_RESAMPLER),
      themeName_(FourCC::VarThemeName, ThemeConstants::DEFAULT_THEME_NAME),
      backlightLevel_(FourCC::VarBacklightLevel, DEFAULT_BACKLIGHT_LEVEL),
      outputVolume_(FourCC::VarOutputVolume, DEFAULT_OUTPUT_VOLUME),
      audioOutput_(FourCC::VarAudioOutput, audioOutputOptions, 3,
                   DEFAULT_AUDIO_OUTPUT),
      recordSource_(FourCC::VarRecordSource, recordSourceOptions, 4, 1),
      recordLineGain_(FourCC::VarRecordLineGain, DEFAULT_RECORD_LINE_GAIN_DB),
      recordMicGain_(FourCC::VarRecordMicGain, DEFAULT_RECORD_MIC_GAIN_DB) {

  variables_.push_back(&background_);
  variables_.push_back(&foreground_);
  variables_.push_back(&hiColor1_);
  variables_.push_back(&hiColor2_);
  variables_.push_back(&consoleColor_);
  variables_.push_back(&cursorColor_);
  variables_.push_back(&infoColor_);
  variables_.push_back(&warnColor_);
  variables_.push_back(&errorColor_);
  variables_.push_back(&accentColor_);
  variables_.push_back(&accentAltColor_);
  variables_.push_back(&emphasisColor_);
  variables_.push_back(&lineOut_);
  variables_.push_back(&midiDevice_);
  variables_.push_back(&midiInputDevice_);
  variables_.push_back(&midiControlSurface_);
  variables_.push_back(&midiSync_);
  variables_.push_back(&remoteUI_);
  variables_.push_back(&instrumentAudition_);
  variables_.push_back(&importResampler_);
  variables_.push_back(&themeName_);
  variables_.push_back(&backlightLevel_);
  variables_.push_back(&outputVolume_);
  variables_.push_back(&audioOutput_);
  variables_.push_back(&recordSource_);
  variables_.push_back(&recordLineGain_);
  variables_.push_back(&recordMicGain_);

  PersistencyDocument doc;

  if (!doc.Load(CONFIG_FILE_PATH)) {
    Trace::Error("CONFIG Could not open file for reading: %s",
                 CONFIG_FILE_PATH);
    Save(); // and write the defaults to SDCard
    return;
  }

  bool elem = doc.FirstChild();
  if (!elem || strcmp(doc.ElemName(), "CONFIG")) {
    Trace::Log("CONFIG", "Bad config.xml format!");
    // TODO: need show user some UI that config file is invalid
    Save(); // and write the defaults to SDCard
    return;
  }

  elem = doc.FirstChild(); // now get first child element of CONFIG
  while (elem) {
    // Check if the parameter exists in our parameters list
    bool paramFound = false;
    for (const auto &param : configParams) {
      if (strcmp(doc.ElemName(), param.name) == 0) {
        paramFound = true;
        break;
      }
    }

    // Special handling for Color elements
    if (strcmp(doc.ElemName(), "Color") == 0) {
      // Process Color element
      ReadColorVariable(&doc);
      elem = doc.NextSibling();
      continue;
    }

    if (!paramFound) {
      Trace::Log("CONFIG", "Found unknown config parameter \"%s\", skipping...",
                 doc.ElemName());
      elem = doc.NextSibling();
      continue;
    }
    bool hasAttr = doc.NextAttribute();
    while (hasAttr) {
      // Special handling for Theme Name sadly because it is a string and no
      // easy way to look that that up in configParams data above
      if (!strcmp(doc.ElemName(), "THEMENAME")) {
        if (Variable *themeVar = FindVariable(FourCC::VarThemeName)) {
          themeVar->SetString(doc.attrval_);
          Trace::Log("CONFIG", "Read Theme Name:%s", doc.attrval_);
        }
      } else {
        // Find the variable by name in configParams
        for (const auto &param : configParams) {
          if (!strcmp(doc.ElemName(), param.name)) {
            if (Variable *var = FindVariable(param.fourcc)) {
              int value = atoi(doc.attrval_);
              if (param.fourcc == FourCC::VarImportResampler) {
                value = NormalizeImportResamplerValue(value);
              } else if (param.fourcc == FourCC::VarMidiControlSurface) {
                value = NormalizeMidiControlSurfaceValue(value);
              }
              var->SetInt(value);
              Trace::Log("CONFIG", "Set %s = %s", param.name, doc.attrval_);
            }
            break;
          }
        }
      }
      hasAttr = doc.NextAttribute();
    }
    elem = doc.NextSibling();
  }
  Trace::Log("CONFIG", "Loaded successfully");
}

Config::~Config() {}

bool Config::IsInstrumentAuditionEnabled() {
  return instrumentAudition_.GetBool();
}

bool Config::Save() {
  auto fs = FileSystem::GetInstance();
  auto fp = fs->Open(CONFIG_FILE_PATH, "w");
  if (!fp) {
    Trace::Error("Could not open file for writing: %s", CONFIG_FILE_PATH);
    return false;
  }
  Trace::Log("PERSISTENCYSERVICE", "Opened Proj File: %s", CONFIG_FILE_PATH);
  tinyxml2::XMLPrinter printer(fp.get());

  SaveContent(&printer);

  return fp->Sync();
}

// Write color variables to an XMLPrinter using the same format as in
// SaveContent
void Config::WriteColorVariables(tinyxml2::XMLPrinter *printer) {
  auto it = variables_.begin();
  for (size_t i = 0; i < variables_.size(); i++) {
    Variable *var = *it;
    FourCC id = var->GetID();

    // Check if this is a color variable
    if (isThemeColorVariable(id)) {

      // Open a Color element
      printer->OpenElement("Color");

      // Add name attribute
      printer->PushAttribute("name", var->GetName());

      // Format color value in hex format with # prefix
      char hexValue[16];
      npf_snprintf(hexValue, sizeof(hexValue), "#%X", var->GetInt());

      // Add value attribute in hex format
      printer->PushAttribute("value", hexValue);

      // Close the Color element
      printer->CloseElement();
    }
    it++;
  }
}

void Config::ReadColorVariable(PersistencyDocument *doc) {
  // Process the current element if it's a Color element
  if (strcmp(doc->ElemName(), "Color") == 0) {
    // Process Color element
    char colorName[64] = {0};
    char colorValue[64] = {0};

    // Get the name and value attributes
    while (doc->NextAttribute()) {
      if (strcmp(doc->attrname_, "name") == 0) {
        // Use safer string copy to ensure null-termination
        size_t len = strlen(doc->attrval_);
        if (len >= sizeof(colorName)) {
          len = sizeof(colorName) - 1; // Truncate if too long
        }
        memcpy(colorName, doc->attrval_, len);
        colorName[len] = '\0'; // Ensure null-termination
      } else if (strcmp(doc->attrname_, "value") == 0) {
        // Use safer string copy to ensure null-termination
        size_t len = strlen(doc->attrval_);
        if (len >= sizeof(colorValue)) {
          len = sizeof(colorValue) - 1; // Truncate if too long
        }
        memcpy(colorValue, doc->attrval_, len);
        colorValue[len] = '\0'; // Ensure null-termination
      }
    }

    // If we have both name and value, set the variable
    if (colorName[0] != '\0' && colorValue[0] != '\0') {
      // Parse the color value (hex string)
      int value = 0;
      bool parsedSuccessfully = false;

      // Handle both formats: with # prefix and without
      if (colorValue[0] == '#' && sscanf(colorValue + 1, "%x", &value) == 1) {
        // Successfully parsed hex value with # prefix
        parsedSuccessfully = true;
      } else if (sscanf(colorValue, "%x", &value) == 1) {
        // Successfully parsed hex value without prefix
        parsedSuccessfully = true;
      } else {
        // Try decimal parsing for backward compatibility
        value = atoi(colorValue);
        if (value > 0) {
          parsedSuccessfully = true;
        }
      }

      if (parsedSuccessfully) {
        // Find the variable by name and set its value
        FourCC fourcc = FourCC::Default; // Use Default as invalid marker

        // Only support uppercase color names for consistency
        if (strcmp(colorName, "BACKGROUND") == 0) {
          fourcc = FourCC::VarBGColor;
        } else if (strcmp(colorName, "FOREGROUND") == 0) {
          fourcc = FourCC::VarFGColor;
        } else if (strcmp(colorName, "HICOLOR1") == 0) {
          fourcc = FourCC::VarHI1Color;
        } else if (strcmp(colorName, "HICOLOR2") == 0) {
          fourcc = FourCC::VarHI2Color;
        } else if (strcmp(colorName, "CONSOLECOLOR") == 0) {
          fourcc = FourCC::VarConsoleColor;
        } else if (strcmp(colorName, "CURSORCOLOR") == 0) {
          fourcc = FourCC::VarCursorColor;
        } else if (strcmp(colorName, "INFOCOLOR") == 0) {
          fourcc = FourCC::VarInfoColor;
        } else if (strcmp(colorName, "WARNCOLOR") == 0) {
          fourcc = FourCC::VarWarnColor;
        } else if (strcmp(colorName, "ERRORCOLOR") == 0) {
          fourcc = FourCC::VarErrorColor;
        } else if (strcmp(colorName, "ACCENTCOLOR") == 0) {
          fourcc = FourCC::VarAccentColor;
        } else if (strcmp(colorName, "ACCENTALTCOLOR") == 0) {
          fourcc = FourCC::VarAccentAltColor;
        } else if (strcmp(colorName, "EMPHASISCOLOR") == 0) {
          fourcc = FourCC::VarEmphasisColor;
        }
        //  else if (strcmp(colorName, "RESERVED1") == 0) {
        //   fourcc = FourCC::VarReserved1Color;
        // } else if (strcmp(colorName, "RESERVED2") == 0) {
        //   fourcc = FourCC::VarReserved2Color;
        // } else if (strcmp(colorName, "RESERVED3") == 0) {
        //   fourcc = FourCC::VarReserved3Color;
        // } else if (strcmp(colorName, "RESERVED4") == 0) {
        //   fourcc = FourCC::VarReserved4Color;
        // }

        if (fourcc != FourCC::Default) { // If we found a valid color
          Variable *var = FindVariable(fourcc);
          if (var) {
            var->SetInt(value);
            Trace::Log("CONFIG", "Read Color: %s = %d", colorName, value);
          }
        }
      }
    }
  }
}

bool Config::SaveTheme(tinyxml2::XMLPrinter *printer, const char *themeName) {
  Trace::Log("CONFIG", "Saving theme content to XML");

  // Open the THEME root element
  printer->OpenElement("THEME");

  // We don't need to save the theme name in the file
  // The filename itself serves as the theme name

  // Write color variables
  WriteColorVariables(printer);

  // Close the THEME root element
  printer->CloseElement(); // THEME

  return true;
}

void Config::SaveContent(tinyxml2::XMLPrinter *printer) {
  // Log the number of variables in the list before saving
  Trace::Log("CONFIG", "Saving %d variables to config file", variables_.size());

  // store config version
  printer->OpenElement("CONFIG");
  printer->PushAttribute("VERSION", CONFIG_VERSION_NUMBER);
  // save all of the config parameters
  auto it = variables_.begin();
  for (size_t i = 0; i < variables_.size(); i++) {
    Variable *var = *it;
    FourCC id = var->GetID();

    // Skip color variables as they will be handled by WriteColorVariables
    if (isThemeColorVariable(id) ||
        id == FourCC::VarReserved1Color || id == FourCC::VarReserved2Color ||
        id == FourCC::VarReserved3Color || id == FourCC::VarReserved4Color) {
      it++;
      continue;
    }

    etl::string<16> elemName = var->GetName();
    to_upper_case(elemName);

    printer->OpenElement(elemName.c_str());
    // these settings need to be saved as the Int values not as String
    // values hence we *dont* use GetString() !
    if (var->GetType() == Variable::CHAR_LIST) {
      char buf[16];
      npf_snprintf(buf, sizeof(buf), "%d", var->GetInt());
      printer->PushAttribute("VALUE", buf);
    } else {
      // all other settings need to be saved as thier String values
      printer->PushAttribute("VALUE", var->GetString().c_str());
    }
    printer->CloseElement();
    it++;
  }

  // Write color variables using the dedicated method
  WriteColorVariables(printer);

  printer->CloseElement();
  Trace::Log("CONFIG", "Saved config");
};

bool Config::LoadTheme(PersistencyDocument *doc) {
  Trace::Log("CONFIG", "Loading theme content from XML");

  // Find the THEME root element
  if (!doc->FirstChild() || strcmp(doc->ElemName(), "THEME") != 0) {
    Trace::Error("Could not find THEME element in document");
    return false;
  }

  // Enter the THEME element to find its children
  if (doc->FirstChild()) {
    // Process all child elements of THEME
    do {
      char *elemName = doc->ElemName();
      Trace::Log("CONFIG", "Processing element: %s", elemName);

      // Unknown elements, including legacy Font settings, are ignored.
      if (strcmp(elemName, "Color") == 0) {
        Trace::Log("CONFIG", "Found Color element");

        // Process this color element directly
        ReadColorVariable(doc);
      } else {
        // Consume obsolete/unknown leaf elements before advancing siblings.
        while (doc->NextAttribute()) {
        }
      }
    } while (doc->NextSibling());
  }
  return true;
}

void Config::ResetTheme() {
  for (FourCC::enum_type colorID : THEME_COLOR_VARIABLE_IDS) {
    if (Variable *variable = FindVariable(colorID)) {
      variable->Reset();
    }
  }

  if (Variable *name = FindVariable(FourCC::VarThemeName)) {
    name->Reset();
  }
}

int Config::GetValue(const char *key) {
  Variable *v = FindVariable(key);
  if (v) {
    Trace::Log("CONFIG", "Got value for %s=%s", key, v->GetString().c_str());
  } else {
    Trace::Log("CONFIG", "No value for requested key:%s", key);
  }
  return v ? v->GetInt() : 0;
};

bool Config::ExportTheme(const char *themeName, bool overwrite) {
  auto fs = FileSystem::GetInstance();

  // Add .ptt extension to the filename
  etl::string<MAX_THEME_NAME_LENGTH> filename = themeName;
  filename.append(THEME_FILE_EXTENSION);

  // Create themes directory if it doesn't exist
  if (!fs->exists(THEMES_DIR)) {
    Trace::Error("Expected themes directory doesn't exist!");
    return false;
  }

  // Build the full path to the theme file
  etl::string<MAX_THEME_EXPORT_PATH_LENGTH> path = THEMES_DIR;
  path.append("/");
  path.append(filename);

  // Check if the file already exists and we're not overwriting
  if (fs->exists(path.c_str()) && !overwrite) {
    Trace::Error("Theme file already exists: %s", path.c_str());
    return false;
  }

  // Open the file for writing
  auto fp = fs->Open(path.c_str(), "w");
  if (!fp) {
    Trace::Error("Failed to open theme file for writing: %s", path.c_str());
    return false;
  }

  tinyxml2::XMLPrinter printer(fp.get());

  // Use the SaveTheme method to save the theme data
  SaveTheme(&printer, themeName);

  Trace::Log("CONFIG", "Successfully exported theme to: %s", path.c_str());
  return true;
}

bool Config::ImportTheme(const char *themeName) {
  auto fs = FileSystem::GetInstance();

  FileSystemPath::PathString path;
  if (!resolveThemePath(themeName, path)) {
    Trace::Error("Failed to resolve theme path for %s",
                 themeName ? themeName : "(null)");
    return false;
  }

  etl::string<MAX_THEME_NAME_LENGTH> baseThemeName;
  if (!extractThemeName(path.c_str(), baseThemeName)) {
    Trace::Error("Failed to extract theme name from %s", path.c_str());
    return false;
  }

  // Sanity check if the file exists
  if (!fs->exists(path.c_str())) {
    Trace::Error("Theme file does not exist: %s", path.c_str());
    return false;
  }

  // Create a persistency document from the file
  PersistencyDocument doc;
  if (!doc.Load(path.c_str())) {
    Trace::Error("Failed to load theme document: %s", path.c_str());
    return false;
  }

  // Store the theme name in the config
  Variable *themeVar = FindVariable(FourCC::VarThemeName);
  themeVar->SetString(baseThemeName.c_str());

  // Use the LoadTheme method to load the theme data
  bool result = LoadTheme(&doc);

  // Save the config to persist the theme name
  if (result) {
    Save();
  }

  return result;
}
