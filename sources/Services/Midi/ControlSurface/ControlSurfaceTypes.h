/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _CONTROL_SURFACE_TYPES_H_
#define _CONTROL_SURFACE_TYPES_H_

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum UsbHostControlSurfaceType {
  USB_HOST_CONTROL_SURFACE_NONE = 0,
  USB_HOST_CONTROL_SURFACE_APC_MINI_MK2 = 1,
  USB_HOST_CONTROL_SURFACE_LAUNCHPAD_MINI_MK3 = 2,
} UsbHostControlSurfaceType;

UsbHostControlSurfaceType usbHostControlSurfaceType(void);
bool usbHostModeActive(void);

#ifdef __cplusplus
}
#endif

#endif
