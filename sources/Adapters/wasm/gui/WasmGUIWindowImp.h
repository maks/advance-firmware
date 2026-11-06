/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 */

#ifndef WASM_GUI_WINDOW_IMP_H_
#define WASM_GUI_WINDOW_IMP_H_

#include "System/Display/Display.h"
#include "UIFramework/Interfaces/I_GUIWindowImp.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <emscripten/html5.h>

class WasmGUIWindowImp : public I_GUIWindowImp {
public:
  static constexpr uint32_t CanvasWidth = 720;
  static constexpr uint32_t CanvasHeight = 720;
  static constexpr size_t FrameBytes = CanvasWidth * CanvasHeight * 4;
  using RgbaFrame = std::array<uint8_t, FrameBytes>;

  WasmGUIWindowImp(GUICreateWindowParams &p);
  virtual ~WasmGUIWindowImp();

  void SetColor(GUIColor &) override;
  void DrawRect(GUIRect &) override;
  void DrawChar(const char c, const GUIPoint &pos,
                const GUITextProperties &) override;
  void DrawString(const char *string, const GUIPoint &pos,
                  const GUITextProperties &, bool overlay = false) override;
  GUIRect GetRect() override;
  void Invalidate() override;
  void Flush() override;
  void Lock() override;
  void Unlock() override;
  void Clear(GUIColor &, bool overlay = false) override;
  void ClearTextRect(GUIRect &) override;
  void PushEvent(GUIEvent &event) override;

  static void ProcessButtonChange(uint16_t changeMask, uint16_t buttonMask);
  static WasmGUIWindowImp *GetInstance();

  void ClockTick();
  void UpdateFrame(bool redraw);
  bool HasPresentedFrame() const;

protected:
  static color_t GetColor(GUIColor &c);

private:
  bool InitializePresenter();
  void DestroyPresenter();
  bool PresentFrame(const RgbaFrame &frame);

  static WasmGUIWindowImp *instance_;
  uint8_t lastColorIdx_ = 0;
  uint16_t foregroundColor565_ = 0;
  uint16_t backgroundColor565_ = 0;
  bool hasPresentedFrame_ = false;

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
  unsigned int shaderProgram_ = 0;
  unsigned int texture_ = 0;
  unsigned int vertexBuffer_ = 0;
  int positionLocation_ = -1;
  int textureLocation_ = -1;

  RgbaFrame rgbaFrame_{};
};

#endif
