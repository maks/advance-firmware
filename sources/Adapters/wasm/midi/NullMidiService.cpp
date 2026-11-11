/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "NullMidiService.h"
#include "Services/Midi/ControlSurface/ControlSurfaceTypes.h"

extern "C" {
UsbHostControlSurfaceType usbHostControlSurfaceType(void) {
  return USB_HOST_CONTROL_SURFACE_NONE;
}

bool usbHostModeActive(void) { return false; }
}
