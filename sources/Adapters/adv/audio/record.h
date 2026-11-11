/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */
#ifndef _RECORD_H_
#define _RECORD_H_

#include "FreeRTOS.h"
#include "Services/Audio/AudioRecording.h"
#include "semphr.h"

#define MAX_INT32 0xFFFFFFFF

#define RECORD_BUFFER_SIZE 35280 // Recording buffer is 200ms long
extern uint16_t recordBuffer[RECORD_BUFFER_SIZE];

static StackType_t RecordStack[1024];
static StaticTask_t RecordTCB;
extern TaskHandle_t RecordHandle;

void Record(void *);

// SAI block B receive callbacks, routed here by the shared SAI callback
// dispatcher while the record path owns the peripheral.
void RecordRxHalfCplt();
void RecordRxCplt();

#endif
