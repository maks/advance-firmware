/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _LINE_IN_SOURCE_H_
#define _LINE_IN_SOURCE_H_

#include <cstdint>

// Live converter frames offered to the mixer by the platform. Frames are
// interleaved 16 bit stereo (L, R) at the mixer sample rate and are sample
// locked to the physical output stream, so no rate conversion is needed.
class LineInSource {
public:
  virtual ~LineInSource() = default;

  // Stereo frames captured and not yet consumed.
  virtual uint32_t availableFrames() const = 0;

  // Consumes up to maxFrames into dst (interleaved L, R) oldest first and
  // returns the number of frames actually copied.
  virtual uint32_t readFrames(int16_t *dst, uint32_t maxFrames) = 0;

  // Discards up to frames oldest frames without copying them out.
  virtual void skipFrames(uint32_t frames) = 0;

  // Tells the platform whether a consumer wants the converter. The default
  // implementation assumes a platform with no converter to power manage.
  virtual void setCaptureRequested(bool requested) { (void)requested; }
};

#endif
