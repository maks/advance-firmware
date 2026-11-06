/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SAMPLE_INDEX_FIELD_H_
#define _SAMPLE_INDEX_FIELD_H_

#include "BaseClasses/UIIntVarField.h"
#include "Externals/etl/include/etl/delegate.h"

#include <cstdint>

class SampleIndexField : public UIIntVarField {
public:
  using ChangeRequest = etl::delegate<bool(int32_t)>;

  SampleIndexField(const GUIPoint &position, Variable &variable,
                   int32_t maximum, ChangeRequest changeRequest);

  void Draw(GUIWindow &w, int offset = 0) override;
  void ProcessArrow(unsigned short mask) override;
  void ProcessClear() override;

private:
  void requestChange(int32_t value);

  ChangeRequest changeRequest_;
};

#endif
