/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstddef>
#include <cstdint>

class WasmDisplayBackend {
public:
  static constexpr uint32_t Width = 720;
  static constexpr uint32_t Height = 720;
  static constexpr uint32_t FramebufferSize = Width * Height;
  static constexpr uint32_t RgbaBufferSize = FramebufferSize * 4;

  static const uint16_t *GetFramebuffer();
  static uint16_t *GetMutableFramebuffer();
  static bool IsDirty();
  static void ClearDirty();
  static void MarkDirty();
  static void CaptureFrameRgba(uint8_t *destination, size_t bufferSize);
};
