/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef TEXT_REGION_CONFIG_H
#define TEXT_REGION_CONFIG_H

// Shared text-cell budget for ALL regions of the active screen. Increase this
// one setting when adding denser layouts/fonts. All six byte arrays and the
// dirty bitmap resize together: approximately 6.125 DTCMRAM bytes per cell.
// Placement is already pixel-addressable; this controls storage, not a grid
// step.
#ifndef TEXT_REGION_CAPACITY
#define TEXT_REGION_CAPACITY 1152
#endif

// Prepared descriptors are reserved once, at 24 bytes each. The current dirty
// region mask supports up to eight regions.
#ifndef TEXT_REGION_LIMIT
#define TEXT_REGION_LIMIT 8
#endif

#if TEXT_REGION_CAPACITY < 1
#error "Text cell capacity must be positive"
#endif
// Leave room for one row beyond the last cell in uint16_t coalescing probes.
#if TEXT_REGION_CAPACITY > (65535 - 255)
#error "Text cell capacity exceeds the renderer's uint16_t addressing limit"
#endif
#if TEXT_REGION_LIMIT < 1 || TEXT_REGION_LIMIT > 8
#error "Text region limit must be between 1 and 8 for the current dirty mask"
#endif

#endif
