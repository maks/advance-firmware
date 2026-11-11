/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef PICOTRACKER_MEMORY_SECTIONS_H
#define PICOTRACKER_MEMORY_SECTIONS_H

#if defined(ADV) && !defined(HOST_TEST) && !defined(WASM)
#define PICOTRACKER_FAST_CODE(name) __attribute__((section(".fastcode." name)))
// The linker marks this region NOLOAD. Use only for buffers whose contents
// are written before being read; keep initialized state in normal RAM.
#define PICOTRACKER_D2_BUFFER __attribute__((section(".RAM_D2"), aligned(32)))
#else
#define PICOTRACKER_FAST_CODE(name)
#define PICOTRACKER_D2_BUFFER
#endif

#endif
