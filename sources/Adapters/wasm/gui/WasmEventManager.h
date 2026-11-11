/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef WASM_EVENT_MANAGER_H_
#define WASM_EVENT_MANAGER_H_

#include "Adapters/wasm/input/InputRepeatTracker.h"
#include "Foundation/T_Singleton.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"

#include <atomic>
#include <cstdint>

class WasmEventManager final : public T_Singleton<WasmEventManager>,
                               public EventManager {
public:
  WasmEventManager();
  ~WasmEventManager() override;

  bool Init() override;
  int MainLoop() override;
  void PostQuitMessage() override;
  int GetKeyCode(const char *name) override;

  void PumpFrame();

private:
  static void RunFrame(void *context);

  std::atomic<bool> finished_{false};
  bool booting_ = true;
  double nextTick_ = 0.0;
  InputRepeatTracker repeatTracker_{};
};

#endif
