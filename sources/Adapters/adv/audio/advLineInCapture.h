/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _ADV_LINE_IN_CAPTURE_H_
#define _ADV_LINE_IN_CAPTURE_H_

#include "Services/Audio/LineInSource.h"
#include "main.h" // must precede FreeRTOS.h for SystemCoreClock linkage

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <cstdint>

// Supplies the live line input to the master output mix on the Advance board.
//
// SAI block B receives the codec data. It is clocked internally from block A,
// so the capture rate is locked to the output stream and the mixer can consume
// frames without any rate conversion.
//
// The record path uses the same peripheral for a capture session. While such a
// session is active this source stops its own transfer and yields no frames,
// then re-acquires the codec and the peripheral when the session ends.
class AdvLineInCapture : public LineInSource {
public:
  // Depth of the circular capture buffer in stereo frames, about 186 ms. The
  // mix reader keeps a target backlog well inside this, so the writer can lap
  // it only if the render pass stalls for longer than the slack.
  static constexpr uint32_t kRingFrames = 8192;

  static AdvLineInCapture &GetInstance();

  // LineInSource
  uint32_t availableFrames() const override;
  uint32_t readFrames(int16_t *dst, uint32_t maxFrames) override;
  void skipFrames(uint32_t frames) override;
  void setCaptureRequested(bool requested) override;

  // Called by the record path around a capture session. Suspending happens
  // synchronously so the record path always gets a free peripheral.
  bool setRecordSessionActive(bool active);

  // True while this source owns the SAI block B transfer.
  static bool ownsTransfer();

  // SAI block B receive DMA callback. The transfer is circular, so only the
  // lap boundary needs handling; the write pointer itself comes from the DMA
  // counter.
  static void RxComplete();

private:
  AdvLineInCapture() = default;

  static void Task(void *argument);
  void process();
  void startCapture();
  void stopCapture();
  void notifyTask();

  // Absolute position of the DMA write pointer in frames, counted from the
  // point the transfer started.
  uint32_t writePositionFrames() const;
  uint32_t readablePositionFrames(uint32_t produced) const;

  static AdvLineInCapture *instance_;

  volatile bool requested_ = false;
  volatile bool recordSessionActive_ = false;
  volatile bool running_ = false;

  // Frame count reached at the start of the current ring lap. The rest of the
  // write pointer position is read from the DMA counter.
  volatile uint32_t lapBaseFrames_ = 0;
  uint32_t readPositionFrames_ = 0;

  TaskHandle_t taskHandle_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;
};

#endif
