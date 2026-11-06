/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmDisplayBackend.h"
#include "Adapters/wasm/platform/WasmBrowserSnapshots.h"

#include <atomic>
#include <cstdint>

namespace {
alignas(uint32_t) std::atomic<uint32_t> g_frameSequence{0};
uint16_t g_wasm_framebuffer[WasmDisplayBackend::FramebufferSize] = {0};
bool g_display_dirty = false;
} // namespace

extern "C" {
uint16_t *wasm_display_get_framebuffer(void) { return g_wasm_framebuffer; }

bool wasm_display_is_dirty(void) { return g_display_dirty; }

void wasm_display_clear_dirty(void) { g_display_dirty = false; }

void wasm_display_mark_dirty(void) {
  g_display_dirty = true;
  g_frameSequence.fetch_add(2, std::memory_order_release);
}
}

const uint16_t *WasmDisplayBackend::GetFramebuffer() {
  return g_wasm_framebuffer;
}

uint16_t *WasmDisplayBackend::GetMutableFramebuffer() {
  return g_wasm_framebuffer;
}

bool WasmDisplayBackend::IsDirty() { return wasm_display_is_dirty(); }

void WasmDisplayBackend::ClearDirty() { wasm_display_clear_dirty(); }

void WasmDisplayBackend::MarkDirty() { wasm_display_mark_dirty(); }

const uint8_t *Wasm_FrameSnapshotAddress() noexcept {
  return reinterpret_cast<const uint8_t *>(g_wasm_framebuffer);
}

const uint32_t *Wasm_FrameSequenceAddress() noexcept {
  return reinterpret_cast<const uint32_t *>(&g_frameSequence);
}

void WasmDisplayBackend::CaptureFrameRgba(uint8_t *dest, size_t bufferSize) {
  if (dest == nullptr || bufferSize < RgbaBufferSize) {
    return;
  }
  const uint16_t *fb = GetFramebuffer();
  for (size_t i = 0; i < FramebufferSize; ++i) {
    const uint16_t pixel = fb[i];
    // Framebuffer is BGR565: bits 11..15 are Blue, 5..10 are Green, 0..4 are
    // Red
    const uint8_t r = (pixel & 0x1FU) << 3U;
    const uint8_t g = ((pixel >> 5U) & 0x3FU) << 2U;
    const uint8_t b = ((pixel >> 11U) & 0x1FU) << 3U;
    dest[i * 4 + 0] = r;
    dest[i * 4 + 1] = g;
    dest[i * 4 + 2] = b;
    dest[i * 4 + 3] = 0xFF;
  }
}
