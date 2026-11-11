/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_PROCESS_H
#define PICOTRACKER_WASM_PROCESS_H

#include <cstdint>

namespace WasmProcess {

bool PowerDown();
bool EnterBootloader();
bool Reboot();

} // namespace WasmProcess

#endif
