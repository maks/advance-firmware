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
#ifndef _CRITICALERRORMESSAGE_H_
#define _CRITICALERRORMESSAGE_H_

#include "System/Display/ErrorBitmap.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_ERROR_MESSAGE_DELAY_SEC 5

__attribute__((noreturn)) void
critical_error_message(const ErrorBitmap *bitmap, const char *title,
                       const char *const *detailLines, size_t detailLineCount,
                       int shutdownDelay);
void critical_sdcard_missing_message(int shutdownDelay);

#ifdef __cplusplus
}
#endif

#endif
