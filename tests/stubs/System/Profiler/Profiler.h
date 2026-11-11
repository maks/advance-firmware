/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#ifndef _PROFILER_H_
#define _PROFILER_H_

class Profiler {
public:
  static Profiler MovingAverage(const char *name) {
    (void)name;
    return Profiler();
  }

  ~Profiler() = default;
};

#endif
