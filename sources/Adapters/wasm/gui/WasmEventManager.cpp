/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "WasmEventManager.h"
#include "Adapters/wasm/audio/WasmAudio.h"
#include "Adapters/wasm/audio/WasmAudioDriver.h"
#include "Adapters/wasm/filesystem/WasmStorageBridge.h"
#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "WasmGUIWindowImp.h"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {
constexpr double ClockIntervalMs = static_cast<double>(PICO_CLOCK_INTERVAL);
}

WasmEventManager::WasmEventManager() = default;
WasmEventManager::~WasmEventManager() = default;

bool WasmEventManager::Init() {
#ifndef HOST_TEST
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
    Trace::Error("WASM_EVENT_MANAGER", "SDL_Init failed: %s", SDL_GetError());
    return false;
  }
#endif
  return true;
}

int WasmEventManager::MainLoop() {
#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop_arg(RunFrame, this, 0, 1);
#endif
  return 0;
}

void WasmEventManager::PostQuitMessage() {
  finished_.store(true, std::memory_order_release);
  PicoTracker_Wasm_RequestShutdown();
}

int WasmEventManager::GetKeyCode(const char * /*name*/) { return -1; }

void WasmEventManager::RunFrame(void *context) {
  auto *manager = static_cast<WasmEventManager *>(context);
  if (manager != nullptr) {
    manager->PumpFrame();
  }
}

void WasmEventManager::PumpFrame() {
  if (booting_) {
    booting_ = false;
    Config *config = Config::GetInstance();
    if (config != nullptr) {
      const int delay = config->GetValue("KEYDELAY");
      const int repeat = config->GetValue("KEYREPEAT");
      repeatTracker_.SetTiming(static_cast<double>(delay),
                               static_cast<double>(repeat));
    }
    PicoTracker_Wasm_MarkReady();
  }

  if (finished_.load(std::memory_order_acquire) ||
      PicoTracker_Wasm_GetState() ==
          static_cast<uint32_t>(WasmRuntimeState::Stopping)) {
#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#endif
    PicoTracker_Wasm_MarkStopped();
    return;
  }

#ifdef __EMSCRIPTEN__
  const double now = emscripten_get_now();
#else
  const double now = 0.0;
#endif

  repeatTracker_.SyncHeldMask(InputMap::GetHeldActionMask());
  repeatTracker_.Process(
      now, [](uint16_t action) { InputMap::RepeatAction(action); });

  InputMap::RetryPendingTransitions();

#ifndef HOST_TEST
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    if (event.type == SDL_QUIT) {
      PostQuitMessage();
    } else if (event.type == SDL_USEREVENT &&
               event.user.code == InputMap::ActionEventCode) {
      uint16_t action = 0;
      bool pressed = false;
      if (InputMap::DecodeActionEvent(
              reinterpret_cast<std::uintptr_t>(event.user.data1), action,
              pressed)) {
        InputMap::AcknowledgeAction(action, pressed);
        uint16_t mask = static_cast<uint16_t>(1U << action);
        WasmGUIWindowImp::ProcessButtonChange(mask, pressed ? mask : 0);
        repeatTracker_.OnAction(action, pressed, now);
      }
    }
  }
#endif

  auto *audioDriver = WasmAudioDriver::Instance();
  if (audioDriver != nullptr) {
    audioDriver->PumpProducer();
  }

  if (now >= nextTick_) {
    nextTick_ = now + ClockIntervalMs;
    auto *window = WasmGUIWindowImp::GetInstance();
    if (window != nullptr) {
      window->ClockTick();
    }
  }

  auto *window = WasmGUIWindowImp::GetInstance();
  if (window != nullptr) {
    window->UpdateFrame(false);
    window->Flush();
  }

  if (audioDriver != nullptr) {
    audioDriver->PumpProducer();
  }

  WasmAudio::PublishSnapshot();

  WasmStorage_FlushMutationNotifications();
}
