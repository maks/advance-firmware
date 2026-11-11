/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "WasmGUIFactory.h"
#include "WasmEventManager.h"
#include "WasmGUIWindowImp.h"
#include <new>

I_GUIWindowImp &WasmGUIFactory::CreateWindowImp(GUICreateWindowParams &p) {
  alignas(
      WasmGUIWindowImp) static unsigned char memBuf[sizeof(WasmGUIWindowImp)];
  return *(new (memBuf) WasmGUIWindowImp(p));
}

EventManager *WasmGUIFactory::GetEventManager() {
  return WasmEventManager::GetInstance();
}
