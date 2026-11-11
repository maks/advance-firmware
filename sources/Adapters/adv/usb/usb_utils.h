/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#ifndef _USB_UTILS_H_
#define _USB_UTILS_H_

#include <cstdint>

// USB MIDI 1.0 table 4-1 uses these CIN values for 1-, 2-, and 3-byte payloads.
constexpr uint8_t usbMidiPayloadLength(uint8_t cin) {
  switch (cin) {
  case 0x2:
  case 0x6:
  case 0xC:
  case 0xD:
    return 2;
  case 0x3:
  case 0x4:
  case 0x7:
  case 0x8:
  case 0x9:
  case 0xA:
  case 0xB:
  case 0xE:
    return 3;
  case 0x5:
  case 0xF:
    return 1;
  default:
    return 0;
  }
}

#include "Services/Midi/ControlSurface/ControlSurfaceTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

void sendUSBMidiMessage(const uint8_t *midicmd, uint8_t len);
void pumpUSBMidiTxQueue(void);
void handleUSBInterrupts();
void resetUSBMidiSoftwareState(void);
bool usbHostMidiMounted(void);
bool usbHostDeviceEnumerated(void);
bool usbHostEnumerationTimedOut(void);
bool usbHostStartupFailed(void);
void usbHostSetStartupFailed(bool failed);
uint32_t usbHostMidiDroppedTxCount(void);
void suppressDwc2OtgInt(void);

#ifdef __cplusplus
}
#endif

#endif
