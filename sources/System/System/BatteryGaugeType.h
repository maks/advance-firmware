/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef BATTERY_GAUGE_TYPE_H
#define BATTERY_GAUGE_TYPE_H

#include <stdint.h>

enum class BatteryGaugeType : uint8_t { Unknown, BQ27441, BQ27220 };

// Set during platform startup after identifying the installed battery gauge.
extern BatteryGaugeType batteryGaugeType;

#endif
