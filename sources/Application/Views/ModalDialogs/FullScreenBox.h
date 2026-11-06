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

#ifndef _FULL_SCREEN_BOX_H_
#define _FULL_SCREEN_BOX_H_

#include "MessageBox.h"
#include "System/Display/ErrorBitmap.h"

class FullScreenBox : public MessageBox {
public:
  static FullScreenBox *Create(View &view, const char *message,
                               int btnFlags = MBBF_OK);
  static FullScreenBox *Create(View &view, const char *message,
                               const char *message2, int btnFlags = MBBF_OK);
  static FullScreenBox *Create(View &view, const ErrorBitmap *bitmap);

  FullScreenBox(View &view, const char *message, int btnFlags = MBBF_OK);
  FullScreenBox(View &view, const char *message, const char *message2,
                int btnFlags = MBBF_OK);
  FullScreenBox(View &view, const ErrorBitmap *bitmap);
  virtual ~FullScreenBox();
  virtual void Destroy() override;

  virtual void DrawView();
  void ProcessButtonMask(unsigned short mask, bool pressed) override;

private:
  const ErrorBitmap *bitmap_ = nullptr;
};

#endif
