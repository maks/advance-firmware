/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#ifndef _FIRMWARE_VERSION_H_
#define _FIRMWARE_VERSION_H_

#include "BuildNumber.h"
#include "FirmwareVersionCode.h"

#include "Externals/etl/include/etl/string.h"
#include "System/Console/nanoprintf.h"

using FirmwareVersionString = etl::string<32>;
using FirmwareNumberString = etl::string<8>; // because max ver is "255.255"

inline FirmwareVersionString FormatFirmwareRelease() {
  const uint32_t version = FIRMWARE_VERSION;
  const unsigned major = (version >> 16) & 0xFF;
  const unsigned minor = (version >> 8) & 0xFF;
  const unsigned patch = version & 0xFF;
  const unsigned type = (version >> 24) & 0xFF;

  const char *typeSuffix = "";
  switch (type) {
  case 0x62:
    typeSuffix = "-Beta";
    break;
  case 0x64:
    typeSuffix = "-Debug";
    break;
  case 0x72:
  default:
    typeSuffix = "";
    break;
  }

  char buffer[FirmwareVersionString::MAX_SIZE + 1];
  npf_snprintf(buffer, sizeof(buffer), "Build %u.%u.%u%s_%s", major, minor,
               patch, typeSuffix, BUILD_COUNT);
  return FirmwareVersionString(buffer);
}

inline FirmwareNumberString FormatFirmwareVersionNumber() {
  const uint32_t version = FIRMWARE_VERSION;
  const unsigned major = (version >> 16) & 0xFF;
  const unsigned minor = (version >> 8) & 0xFF;

  char buffer[FirmwareNumberString::MAX_SIZE + 1];
  npf_snprintf(buffer, sizeof(buffer), "%u.%u", major, minor);
  return FirmwareNumberString(buffer);
}

#endif
