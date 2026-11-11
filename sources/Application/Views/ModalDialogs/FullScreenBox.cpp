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

#include "FullScreenBox.h"
#include <Application/AppWindow.h>
#include <new>

static const char *buttonText[MBL_LAST] = {"Ok", "Yes", "Cancel", "No"};

static bool inUse = false;
alignas(FullScreenBox) static unsigned char FullScreenBoxStorage[sizeof(
    FullScreenBox)];
static void *storage = FullScreenBoxStorage;

FullScreenBox *FullScreenBox::Create(View &view, const char *message,
                                     int btnFlags) {
  if (inUse) {
    auto *existing = reinterpret_cast<FullScreenBox *>(storage);
    existing->~FullScreenBox();
    inUse = false;
  }
  inUse = true;
  return new (storage) FullScreenBox(view, message, btnFlags);
}

FullScreenBox *FullScreenBox::Create(View &view, const char *messageLine1,
                                     const char *messageLine2, int btnFlags) {
  if (inUse) {
    auto *existing = reinterpret_cast<FullScreenBox *>(storage);
    existing->~FullScreenBox();
    inUse = false;
  }
  inUse = true;
  return new (storage)
      FullScreenBox(view, messageLine1, messageLine2, btnFlags);
}

FullScreenBox *FullScreenBox::Create(View &view, const ErrorBitmap *bitmap) {
  if (inUse) {
    auto *existing = reinterpret_cast<FullScreenBox *>(storage);
    existing->~FullScreenBox();
    inUse = false;
  }
  inUse = true;
  return new (storage) FullScreenBox(view, bitmap);
}

FullScreenBox::FullScreenBox(View &view, const char *message, int btnFlags)
    : MessageBox(view, message, btnFlags) {}

FullScreenBox::FullScreenBox(View &view, const char *messageLine1,
                             const char *messageLine2, int btnFlags)
    : MessageBox(view, messageLine1, messageLine2, btnFlags) {}

FullScreenBox::FullScreenBox(View &view, const ErrorBitmap *bitmap)
    : MessageBox(view, "", "", MBBF_OK), bitmap_(bitmap) {}

FullScreenBox::~FullScreenBox(){};

void FullScreenBox::Destroy() {
  this->~FullScreenBox();
  inUse = false;
};

void FullScreenBox::ProcessButtonMask(unsigned short mask, bool pressed) {
  // Bitmap warnings are owned by the battery/SD condition in AppWindow. ENTER
  // must not destroy their modal while that condition still requires it.
  if (!bitmap_)
    MessageBox::ProcessButtonMask(mask, pressed);
}

void FullScreenBox::DrawView() {
  if (bitmap_) {
    return;
  }

  // message size
  int line1_width = line1_.size();
  // set window size full screen
  SetWindow(SCREEN_WIDTH, SCREEN_HEIGHT);

  // draw text
  int x1 = ((SCREEN_WIDTH - line1_width) / 2) - 2;
  int y1 = (SCREEN_HEIGHT / 2) - 4;
  GUITextProperties props;
  SetColor(CD_ERROR);
  DrawString(x1, y1, line1_.c_str(), props);
  if (line2_.size() > 0) {
    int line2_width = line2_.size();
    int x2 = ((SCREEN_WIDTH - line2_width) / 2) - 2;
    int y2 = y1 + 2;
    DrawString(x2, y2, line2_.c_str(), props);
  }
  SetColor(CD_NORMAL);
};
