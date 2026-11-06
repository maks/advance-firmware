/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmProcess.h"

#include "System/Console/Trace.h"
#include <array>
#include <atomic>

namespace {
enum class UnsupportedOperation : std::size_t {
  PowerDown,
  Bootloader,
  Reboot,
  Count,
};

std::array<std::atomic<bool>,
           static_cast<std::size_t>(UnsupportedOperation::Count)>
    logged{};

bool ReportUnsupported(UnsupportedOperation operation, const char *name) {
  bool expected = false;
  if (logged[static_cast<std::size_t>(operation)].compare_exchange_strong(
          expected, true, std::memory_order_relaxed)) {
    Trace::Error("WASM_PROCESS", "unsupported operation: %s", name);
  }
  return false;
}
} // namespace

bool WasmProcess::PowerDown() {
  return ReportUnsupported(UnsupportedOperation::PowerDown, "power-down");
}

bool WasmProcess::EnterBootloader() {
  return ReportUnsupported(UnsupportedOperation::Bootloader, "bootloader");
}

bool WasmProcess::Reboot() {
  return ReportUnsupported(UnsupportedOperation::Reboot, "reboot");
}
