/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#ifndef _EVENT_QUEUE_
#define _EVENT_QUEUE_
#include "FreeRTOS.h"
#include "queue.h"
#include "stm32h7xx.h"

enum EventType {
  REDRAW,
  FLUSH,
  CLOCK,
  BUTTON_CHANGE,
  SD_DET_INSERT,
  SD_DET_REMOVE,
  CHARGER_INT_EVENT,
  USB_ROLE_INT_EVENT,
  LAST
};

class Event {
public:
  Event(EventType type) : type_(type) {}
  Event(EventType type, uint16_t buttonChangeMask, uint16_t buttonMask)
      : type_(type), buttonChangeMask_(buttonChangeMask),
        buttonMask_(buttonMask) {}
  uint32_t queuedCycles_ = DWT->CYCCNT;
  EventType type_;
  uint16_t buttonChangeMask_ = 0;
  uint16_t buttonMask_ = 0;
};

#define EVENT_QUEUE_LENGTH 16
#define EVENT_QUEUE_ITEM_SIZE sizeof(Event)
static StaticQueue_t eventQueueBuffer;
extern uint8_t eventQueueStorage;
extern QueueHandle_t eventQueue;
#endif
