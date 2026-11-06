/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#ifndef _TRACE_H_
#define _TRACE_H_

class Trace {
public:
  static void Debug(const char *fmt, ...) { (void)fmt; }
  static void Log(const char *category, const char *fmt, ...) {
    (void)category;
    (void)fmt;
  }
  static void Error(const char *fmt, ...) { (void)fmt; }
};

#endif
