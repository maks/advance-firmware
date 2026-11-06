/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef WASM_GUI_FACTORY_H_
#define WASM_GUI_FACTORY_H_

#include "UIFramework/Interfaces/I_GUIWindowFactory.h"

class WasmGUIFactory final : public I_GUIWindowFactory {
public:
  WasmGUIFactory() = default;
  ~WasmGUIFactory() override = default;

  I_GUIWindowImp &CreateWindowImp(GUICreateWindowParams &p) override;
  EventManager *GetEventManager() override;
};

#endif
