/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025 STMicroelectronics.
 * Copyright (c) 2025 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef ADV_CORE_INC_FIRMWARE_INFO_H_
#define ADV_CORE_INC_FIRMWARE_INFO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t magic;   // e.g. 0xBEEFC0DE
  uint32_t length;  // Length of firmware in bytes
  uint32_t version; // Semantic version or build ID
  uint32_t crc32;   // CRC of the firmware (excluding this struct)
  uint32_t type;    // type of firmware
} firmware_info_t;

extern const firmware_info_t firmware_info;

#ifdef __cplusplus
}
#endif

#endif
