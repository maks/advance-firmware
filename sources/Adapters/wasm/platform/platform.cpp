/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "platform.h"
#include "Adapters/wasm/mutex/WasmMutex.h"
#include "System/System/System.h"

uint32_t platform_get_rand() {
  System *system = System::GetInstance();
  return system ? system->GetRandomNumber() : 0;
}

void platform_reboot() {
  System *system = System::GetInstance();
  if (system) {
    system->SystemReboot();
  }
}

void platform_bootloader() {
  System *system = System::GetInstance();
  if (system) {
    system->SystemBootloader();
  }
}

SysMutex *platform_mutex() {
  static WasmMutex mutex;
  return &mutex;
}

uint32_t millis(void) {
  System *system = System::GetInstance();
  return system ? system->Millis() : 0;
}

uint32_t micros(void) {
  System *system = System::GetInstance();
  return system ? system->Micros() : 0;
}

void platform_brightness(uint8_t /*value*/) {}

int16_t battery_health() { return -1; }
