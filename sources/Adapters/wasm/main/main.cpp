/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Adapters/wasm/audio/WasmAudio.h"
#include "Adapters/wasm/gui/WasmEventManager.h"
#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Application/Application.h"
#include "UIFramework/BasicDatas/GUICreateWindowParams.h"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include <new>

namespace {
void CompleteFailedStartupShutdown(void *) {
  WasmAudio::StopBrowserAudio();
  if (!WasmAudio::BrowserTeardownComplete()) {
    return;
  }

  WasmSystem::ShutdownPlatformServices();
#ifndef HOST_TEST
  SDL_Quit();
#endif
  PicoTracker_Wasm_MarkStopped();
#ifdef __EMSCRIPTEN__
  emscripten_cancel_main_loop();
#endif
}

int FailStartup(const char *message) {
  PicoTracker_Wasm_Fail(message);
  WasmAudio::StopBrowserAudio();
#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop_arg(CompleteFailedStartupShutdown, nullptr, 0, 1);
#endif
  return 1;
}
} // namespace

int main() {
  if (!WasmSystem::InstallPlatformServices()) {
    return FailStartup("Failed to install browser platform services");
  }

  auto *eventManager = WasmEventManager::GetInstance();
  if (eventManager == nullptr || !eventManager->Init()) {
    return FailStartup("Failed to initialize SDL2 browser UI");
  }

  GUICreateWindowParams params;
  params.title = "picoTracker Advance";

  Application *app = Application::GetInstance();
  if (app == nullptr || !app->Init(params)) {
    return FailStartup("Failed to initialize PicoTracker application");
  }

  eventManager->MainLoop();
  return 0;
}
