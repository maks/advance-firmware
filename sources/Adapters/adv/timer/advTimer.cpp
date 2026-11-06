/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "advTimer.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "System/System/System.h"
#include "task.h"

int64_t advTimerCallback(int32_t interval, void *param) {
  advTimer *timer = (advTimer *)param;
  return timer->OnTimerTick();
};

advTimer::advTimer() {
  period_ = -1;
  timer_ = 0;
  running_ = false;
};

advTimer::~advTimer() {}

void advTimer::SetPeriod(float msec) {
  period_ = int64_t(msec);
  offset_ = 0;
};

bool advTimer::Start() {
  if (period_ > 0) {
    offset_ = period_;
    int64_t newcb = int(offset_);
    offset_ -= newcb;
    // TODO(stm): reenable this! what to do with callback?
    //    timer_ = add_alarm_in_ms(newcb, advTimerCallback, this,
    //    false);
    //    timer_ =
    //        xTimerCreate(/* Just a text name, not used by the RTOS kernel. */
    //                     "advTimer",
    /* The timer period in ticks, must be greater than 0. */
    //                     newcb / portTICK_PERIOD_MS,
    /* The timers will auto-reload themselves when they expire.
     */
    //                     pdTRUE,
    /* The ID is used to store a count of the number of times
       the timer has expired, which is initialised to 0. */
    //                     (void *)0,
    /* Each timer calls the same callback when it expires. */
    //                     advTimerCallback);
    //    auto result = xTimerStart(timer_, 100);
    //    Trace::Log("Timer started: %b", result);
    lastTick_ = System::GetInstance()->GetClock();
    running_ = true;
  }
  return (timer_ != 0);
};

void advTimer::Stop() {
  // TODO(stm): reenable
  //  cancel_alarm(timer_);
  //  timer_ = 0;
  //  auto result = xTimerStop(timer_, 100);
  //  Trace::Log("Timer stopped: %b", result);
  running_ = false;
};

float advTimer::GetPeriod() { return period_; };

int64_t advTimer::OnTimerTick() {
  int64_t newcb = 0;
  if (running_) {
    SetChanged();
    NotifyObservers();
    offset_ += period_;
    newcb = int64_t(offset_);
    offset_ -= newcb;
    NAssert(newcb > 0);
  }
  return newcb;
};

I_Timer *advTimerService::CreateTimer() {
  static advTimer timerInstance;
  timerInstance.Stop();
  timerInstance.SetPeriod(-1.0f);
  return &timerInstance;
};

void advTimerService::TriggerCallback(int msec, timerCallback cb) {
  if (cb == nullptr) {
    Trace::Error("ADV_TIMER", "one-shot callback is null");
    return;
  }

  TickType_t period = pdMS_TO_TICKS(msec > 0 ? msec : 1);
  if (period == 0) {
    period = 1;
  }

  for (auto &context : triggerCallbacks_) {
    taskENTER_CRITICAL();
    const bool available = context.callback == nullptr;
    if (available) {
      context.callback = cb;
    }
    taskEXIT_CRITICAL();

    if (!available) {
      continue;
    }

    if (context.timer == nullptr) {
      context.timer =
          xTimerCreateStatic("advCallback", period, pdFALSE, &context,
                             TriggerCallbackTimer, &context.timerStorage);
    }

    if (context.timer == nullptr ||
        xTimerChangePeriod(context.timer, period, pdMS_TO_TICKS(10)) !=
            pdPASS) {
      taskENTER_CRITICAL();
      context.callback = nullptr;
      taskEXIT_CRITICAL();
      Trace::Error("ADV_TIMER", "failed to schedule one-shot callback");
      cb();
      return;
    }

    return;
  }

  Trace::Error("ADV_TIMER", "one-shot callback slots exhausted");
  cb();
}

void advTimerService::TriggerCallbackTimer(TimerHandle_t timer) {
  auto *context =
      static_cast<TriggerCallbackContext *>(pvTimerGetTimerID(timer));
  if (context == nullptr) {
    return;
  }

  // Keep the slot reserved until the callback returns, so another request
  // cannot reuse its context while it is still executing.
  taskENTER_CRITICAL();
  timerCallback callback = context->callback;
  taskEXIT_CRITICAL();
  if (callback != nullptr) {
    callback();
  }

  taskENTER_CRITICAL();
  context->callback = nullptr;
  taskEXIT_CRITICAL();
}
