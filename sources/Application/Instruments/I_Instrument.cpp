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

#include "I_Instrument.h"
#include "../Model/Project.h"
#include "Application/Model/FirmwareVersion.h"
#include "Application/Player/InsertEffectDefinitions.h"
#include "Application/Utils/char.h"
#include "System/Console/Trace.h"

I_Instrument::~I_Instrument() {
  // Virtual destructor implementation
}

void I_Instrument::SaveContent(tinyxml2::XMLPrinter *printer) {
  // Add firmware version information
  const FirmwareNumberString firmwareVersion = FormatFirmwareVersionNumber();
  printer->PushAttribute("VERSION", firmwareVersion.c_str());
  // Save the instrument type
  printer->PushAttribute("TYPE", InstrumentTypeNames[GetType()]);

  // Save the instrument name as its not stored in the Variables
  if (!name_.empty()) {
    printer->OpenElement("PARAM");
    printer->PushAttribute("NAME", "InstrumentName");
    printer->PushAttribute("VALUE", name_.c_str());
    printer->CloseElement(); // PARAM
  }

  // Save all the instrument's parameters
  for (auto it = Variables()->begin(); it != Variables()->end(); it++) {
    etl::string<MAX_VARIABLE_STRING_LENGTH> value = (*it)->GetString();
    const FourCC id = (*it)->GetID();
    // ensure to use full name not short UI displayname for persistance
    if (id == FourCC::VoiceInsertType || id == FourCC::VoiceInsertType2) {
      value = GetVoiceInsertEffectInfo((*it)->GetInt()).name_;
    }

    printer->OpenElement("PARAM");
    printer->PushAttribute("NAME", (*it)->GetName());
    printer->PushAttribute("VALUE", value.c_str());
    printer->CloseElement(); // PARAM
  }
}

void I_Instrument::RestoreContent(PersistencyDocument *doc) {
  // First, check for TYPE attribute in the INSTRUMENT element if attributes
  // have not already been consumed by a derived instrument.
  if (doc->ElemName() != nullptr &&
      !strcasecmp(doc->ElemName(), "INSTRUMENT") &&
      (doc->r_ == YXML_OK || doc->r_ == YXML_ELEMSTART ||
       doc->r_ == YXML_ATTREND)) {
    bool hasAttr = doc->NextAttribute();
    while (hasAttr) {
      if (!strcasecmp(doc->attrname_, "TYPE")) {
        Trace::Log("I_INSTRUMENT", "Instrument type from XML: %s",
                   doc->attrval_);
        // TODO: We already know the instrument type so need to validate it
        // matches the imported one here
      }
      hasAttr = doc->NextAttribute();
    }
  }

  // Navigate to the first child of the INSTRUMENT element (which should be a
  // PARAM element)
  bool subelem =
      (doc->ElemName() != nullptr && !strcasecmp(doc->ElemName(), "PARAM") &&
       doc->r_ == YXML_ELEMSTART)
          ? true
          : doc->FirstChild();
  int paramCount = 0;

  while (subelem) {
    // Process the PARAM element attributes
    bool hasAttr = doc->NextAttribute();
    char name[MAX_VARIABLE_STRING_LENGTH + 1] = "";
    char value[MAX_VARIABLE_STRING_LENGTH + 1] = "";
    while (hasAttr) {
      if (!strcasecmp(doc->attrname_, "NAME")) {
        strcpy(name, doc->attrval_);
      }
      if (!strcasecmp(doc->attrname_, "VALUE")) {
        strcpy(value, doc->attrval_);
      }
      hasAttr = doc->NextAttribute();
    }

    if (name[0] != '\0' && value[0] != '\0') {
      // Special handling for InstrumentName parameter
      if (!strcasecmp(name, "InstrumentName")) {
        // Set the instrument name directly
        SetName(value);
        Trace::Log("I_INSTRUMENT", "Set instrument name: %s", value);
        paramCount++;
      } else {
        // For other parameters, find the variable and set its value
        bool found = false;

        // Find the variable with this name and set its value
        for (auto it = Variables()->begin(); it != Variables()->end(); it++) {
          if (!strcasecmp((*it)->GetName(), name)) {
            (*it)->SetString(value);
            // Trace::Log("I_INSTRUMENT", "Set parameter: %s = %s", name,
            // value);
            found = true;
            paramCount++;
            break;
          }
        }

        if (!found) {
          Trace::Error("Parameter '%s' not found in instrument", name);
        }
      }
    }

    // Move to the next PARAM element
    subelem = doc->NextSibling();
  }
  // Trace::Log("I_INSTRUMENT", "RestoreContent: Restored %d parameters",
  //            paramCount);

  // Update any UI variables that represent the instrument name
  Variable *nameVar = FindVariable(FourCC::InstrumentName);
  if (nameVar && !name_.empty()) {
    nameVar->SetString(name_.c_str());
  }
}

void I_Instrument::Purge() {
  for (auto it = Variables()->begin(); it != Variables()->end(); it++) {
    (*it)->Reset();
  }
};

void I_Instrument::CloneFrom(I_Instrument &src) {
  SetName(src.GetUserSetName().c_str());
  for (auto it = src.Variables()->begin(); it != src.Variables()->end(); it++) {
    Variable *dstV = FindVariable((*it)->GetID());
    if (dstV) {
      dstV->CopyFrom(**it);
    }
  }
}
