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

#ifndef _MODAL_VIEW_H_
#define _MODAL_VIEW_H_

#include "View.h"
#include <stdint.h>

class ModalView : public View {
public:
  ModalView(View &);
  virtual ~ModalView();

  bool IsFinished();
  int GetReturnCode();
  uint32_t GetInstanceId() const;

  void EndModal(int returnCode);
  virtual void Destroy();

protected:
  void SetWindow(int width, int height);
  virtual void ClearTextRect(int x, int y, int w, int h);
  virtual void DrawString(int x, int y, const char *txt,
                          const GUITextProperties &props);
  virtual bool ShouldDeferPlayButtonInput() const override { return false; }

  // Override GetAnchor to account for modal window position
  GUIPoint GetAnchor() override;

private:
  static uint32_t nextInstanceId_;
  uint32_t instanceId_;
  bool finished_;
  int returnCode_;
  int left_;
  int top_;
};
#endif
