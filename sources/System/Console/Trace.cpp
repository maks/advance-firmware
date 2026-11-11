/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "Trace.h"
#include "Externals/etl/include/etl/error_handler.h"
#include "System/System/System.h"
#ifndef WASM
#include "tim.h"
#endif
#include <string.h>

// be explicit about the nanoprintf configuration
#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#include "nanoprintf.h"

namespace {

#if !defined(NDEBUG) || defined(ENABLE_LOGGING)
uint64_t getMonotonicTimestampUs() {
#if defined(WASM)
  System *sys = System::GetInstance();
  return sys ? static_cast<uint64_t>(sys->Micros()) : 0;
#else
  static uint32_t lastCounter = 0;
  static uint64_t wrapOffset = 0;

  const uint32_t counter = __HAL_TIM_GET_COUNTER(&htim2);
  if (counter < lastCounter) {
    wrapOffset += (1ULL << 32);
  }
  lastCounter = counter;

  return wrapOffset + counter;
#endif
}
#endif

} // namespace

Trace::Trace() {}

void Trace::trace_uart_putc(int c, void *context) {
  System *sys = System::GetInstance();
  sys->SystemPutChar(c);
}

//------------------------------------------------------------------------------

void Trace::VLog(const char *category, const char *fmt, va_list &args) {
#if !defined(NDEBUG) || defined(ENABLE_LOGGING)
  const uint64_t timestampUs = getMonotonicTimestampUs();
  const unsigned long long seconds = timestampUs / 1000000ULL;
  const unsigned long micros =
      static_cast<unsigned long>(timestampUs % 1000000ULL);

  // first prepend the category
  npf_pprintf(&trace_uart_putc, NULL, "[%llu.%06lu] [%s] ", seconds, micros,
              category);

  npf_vpprintf(&trace_uart_putc, NULL, fmt, args);
  // end with NL+CR as thats how it previously worked using stdio' printf
  npf_pprintf(&trace_uart_putc, NULL, "\r\n");
#else
  (void)category;
  (void)fmt;
  (void)args;
#endif
}

//------------------------------------------------------------------------------

void Trace::Log(const char *category, const char *fmt, ...) {
#if !defined(NDEBUG) || defined(ENABLE_LOGGING)
  va_list args;
  va_start(args, fmt);
  VLog(category, fmt, args);
  va_end(args);
#else
  (void)category;
  (void)fmt;
#endif
}

//------------------------------------------------------------------------------

void Trace::Debug(const char *fmt, ...) {
#if !defined(NDEBUG) || defined(ENABLE_LOGGING)
  va_list args;
  va_start(args, fmt);
  VLog("-D-", fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

//------------------------------------------------------------------------------

void Trace::Error(const char *fmt, ...) {
#if !defined(NDEBUG) || defined(ENABLE_LOGGING)
  va_list args;
  va_start(args, fmt);
  VLog("*ERROR*", fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

//------------------------------------------------------------------------------

// Never inline, so you can breakpoint on this function to get backtraces
__attribute__((noinline)) void EtlError(const etl::exception &e) {
  Trace::Error("ETL: %s:%d: %s", e.file_name(), e.line_number(), e.what());
}

void Trace::RegisterEtlErrorHandler() {
#ifdef ETL_LOG_ERRORS
  etl::error_handler::set_callback<EtlError>();
#endif
}

//------------------------------------------------------------------------------
