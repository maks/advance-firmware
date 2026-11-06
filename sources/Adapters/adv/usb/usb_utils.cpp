/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "usb_utils.h"
#include "Adapters/adv/system/advUsbRoleManager.h"
#include "Adapters/adv/system/charger.h"
#include "Adapters/adv/usb/usb_audio_handoff.h"
#include "FreeRTOS.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/Profiler/DWTProfiler.h"
#include "device/dcd.h"
#include "host/hcd.h"
#include "queue.h"
#include "stm32h7xx.h"
#include "task.h"
#include "tusb.h"

#include <string.h>

namespace {
constexpr uint16_t kProfileOrderUsbMidiQueue = 90;
// Launchpad Mini control-surface RGB LED updates use 13-byte SysEx messages.
constexpr uint8_t kUsbMidiTxMaxMsgLen = 13;
// Live-mode refreshes send a large burst of SysEx updates. Keep enough queued
// items for a full Launchpad Mini 8x8 refresh alongside ordinary MIDI traffic.
constexpr UBaseType_t kUsbMidiTxQueueLen = 128;
constexpr uint8_t kInvalidHostMidiIdx = 0xFF;
constexpr uint8_t kPreferredHostMidiSingleCable = 0;
constexpr uint8_t kPreferredHostMidiMultiCable = 1;
constexpr uint32_t kUsbTaskTimeoutMs = 1;
constexpr uint16_t kAkaiVendorId = 0x09E8;
constexpr uint16_t kApcMiniMk2ProductId = 0x004F;
constexpr uint16_t kNovationVendorId = 0x1235;
constexpr uint16_t kLaunchpadMiniMk3ProductId = 0x0113;
constexpr uint8_t kApcMiniMk2LedPortCable = 0;
constexpr TickType_t kUsbHostEnumerationTimeoutTicks = pdMS_TO_TICKS(2000u);

struct UsbMidiTxItem {
  uint8_t len;
  uint8_t data[kUsbMidiTxMaxMsgLen];
};

StaticQueue_t g_usbMidiTxQueueBuffer;
uint8_t g_usbMidiTxQueueStorage[kUsbMidiTxQueueLen * sizeof(UsbMidiTxItem)];
QueueHandle_t g_usbMidiTxQueue = nullptr;

UsbMidiTxItem g_pendingItem = {};
uint8_t g_pendingOffset = 0;
bool g_pendingValid = false;
uint32_t g_usbMidiTxDroppedItems = 0;
bool g_usbMidiTxDropLogged = false;

uint8_t g_hostMidiIdx = kInvalidHostMidiIdx;
bool g_hostMidiMounted = false;
uint8_t g_hostMidiRxCable = kPreferredHostMidiSingleCable;
uint8_t g_hostMidiTxCable = kPreferredHostMidiSingleCable;
uint16_t g_hostMidiVendorId = 0;
uint16_t g_hostMidiProductId = 0;
bool g_hostMidiIsApcMiniMk2 = false;
bool g_usbHostDeviceAttached = false;
bool g_usbHostDeviceEnumerated = false;
TickType_t g_usbHostAttachTick = 0;
bool g_usbHostEnumerationTimedOut = false;
bool g_usbHostStartupFailed = false;

DWTProfileStats gUsbMidiQueueProfile = {
    .name_ = "usb midi q",
    .printLast_ = true,
    .displayOrder_ = kProfileOrderUsbMidiQueue,
};

bool isApcMiniMk2(uint16_t vendorId, uint16_t productId) {
  return (vendorId == kAkaiVendorId) && (productId == kApcMiniMk2ProductId);
}

bool isLaunchpadMiniMk3(uint16_t vendorId, uint16_t productId) {
  return (vendorId == kNovationVendorId) &&
         (productId == kLaunchpadMiniMk3ProductId);
}

void refreshControlSurfaceSelection() {
  auto *midiService = MidiService::GetInstance();
  if (midiService != nullptr) {
    midiService->RefreshControlSurfaceSelection();
  }
}

void resetHostMidiSelection() {
  g_hostMidiIdx = kInvalidHostMidiIdx;
  g_hostMidiMounted = false;
  g_hostMidiRxCable = kPreferredHostMidiSingleCable;
  g_hostMidiTxCable = kPreferredHostMidiSingleCable;
  g_hostMidiVendorId = 0;
  g_hostMidiProductId = 0;
  g_hostMidiIsApcMiniMk2 = false;
}

void resetUsbHostIndicatorState() {
  g_usbHostDeviceAttached = false;
  g_usbHostDeviceEnumerated = false;
  g_usbHostAttachTick = 0;
  g_usbHostEnumerationTimedOut = false;
  g_usbHostStartupFailed = false;
}

void noteUsbHostAttach(bool inIsr) {
  g_usbHostDeviceAttached = true;
  g_usbHostDeviceEnumerated = false;
  g_usbHostAttachTick =
      inIsr ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
  g_usbHostEnumerationTimedOut = false;
  g_usbHostStartupFailed = false;
  Trace::Log("USBHOST", "attach detected tick=%lu", g_usbHostAttachTick);
}

void noteUsbHostRemove() {
  Trace::Log("USBHOST", "device removed");
  resetUsbHostIndicatorState();
}

void updateUsbHostEnumerationTimeoutState() {
  if (tuh_inited()) {
    const AdvUsbTypeCStatus *status = advUsbRoleManagerGetStatus();
    const bool physicallyAttached = advUsbStatusRequestsHostPower(status) &&
                                    (status != nullptr) && status->attached;

    if (physicallyAttached && !g_usbHostDeviceAttached) {
      noteUsbHostAttach(false);
      Trace::Log("USBHOST", "attach inferred from USB role manager");
    }

    if (!physicallyAttached && g_usbHostDeviceAttached &&
        !g_usbHostDeviceEnumerated) {
      Trace::Log("USBHOST", "detach inferred from USB role manager");
      noteUsbHostRemove();
    }
  }

  if (!tuh_inited() || !g_usbHostDeviceAttached || g_usbHostDeviceEnumerated ||
      g_usbHostEnumerationTimedOut) {
    return;
  }

  const TickType_t elapsed = xTaskGetTickCount() - g_usbHostAttachTick;
  if (elapsed < kUsbHostEnumerationTimeoutTicks) {
    return;
  }

  g_usbHostEnumerationTimedOut = true;
  Trace::Error("USBHOST: enumeration timed out after %lu ms",
               static_cast<unsigned long>(pdTICKS_TO_MS(elapsed)));
  logHostPowerDiagnostics("usb-enum-timeout");
}

void selectHostMidiInterface(uint8_t idx, uint8_t interfaceNumber) {
  if (g_hostMidiMounted && (g_hostMidiIdx != kInvalidHostMidiIdx) &&
      (g_hostMidiIdx != idx)) {
    tuh_itf_info_t currentInfo{};
    if (tuh_midi_itf_get_info(g_hostMidiIdx, &currentInfo)) {
      if (currentInfo.desc.bInterfaceNumber >= interfaceNumber) {
        return;
      }
    }
  }

  g_hostMidiIdx = idx;
  g_hostMidiMounted = true;
  g_hostMidiRxCable = kPreferredHostMidiSingleCable;
  g_hostMidiTxCable = kPreferredHostMidiSingleCable;
}

uint8_t preferredHostMidiCable(uint8_t cableCount) {
  if (g_hostMidiIsApcMiniMk2) {
    return kApcMiniMk2LedPortCable;
  }

  if (cableCount > 1u) {
    return kPreferredHostMidiMultiCable;
  }

  return kPreferredHostMidiSingleCable;
}
void configureHostMidiCables(uint8_t rxCableCount, uint8_t txCableCount) {
  g_hostMidiRxCable = preferredHostMidiCable(rxCableCount);
  g_hostMidiTxCable = preferredHostMidiCable(txCableCount);
}

extern "C" void advUSBMidiHostFeedRxBytes(const uint8_t *bytes, uint16_t count);

void ensureUsbMidiTxQueue() {
  if (g_usbMidiTxQueue != nullptr) {
    return;
  }

  taskENTER_CRITICAL();
  if (g_usbMidiTxQueue == nullptr) {
    g_usbMidiTxQueue =
        xQueueCreateStatic(kUsbMidiTxQueueLen, sizeof(UsbMidiTxItem),
                           g_usbMidiTxQueueStorage, &g_usbMidiTxQueueBuffer);
  }
  taskEXIT_CRITICAL();
}

bool canQueueUsbMidiTx() {
  if (tuh_inited()) {
    return g_hostMidiMounted && (g_hostMidiIdx != kInvalidHostMidiIdx) &&
           tuh_midi_mounted(g_hostMidiIdx);
  }

  if (tud_inited()) {
    return tud_midi_n_mounted(0);
  }

  return false;
}

bool pumpNextItemDevice() {
  if (!tud_midi_n_mounted(0)) {
    return false;
  }

  const uint8_t remaining = g_pendingItem.len - g_pendingOffset;
  if (remaining == 0u) {
    g_pendingValid = false;
    return true;
  }

  uint32_t written = 0;
  taskENTER_CRITICAL();
  written = tud_midi_n_stream_write(0, 0, g_pendingItem.data + g_pendingOffset,
                                    remaining);
  taskEXIT_CRITICAL();

  if (written == 0u) {
    return false;
  }

  g_pendingOffset += static_cast<uint8_t>(written);
  if (g_pendingOffset >= g_pendingItem.len) {
    g_pendingValid = false;
    g_pendingOffset = 0;
  }

  return true;
}

bool pumpNextItemHost() {
  if (!g_hostMidiMounted || (g_hostMidiIdx == kInvalidHostMidiIdx) ||
      !tuh_midi_mounted(g_hostMidiIdx)) {
    return false;
  }

  const uint8_t remaining = g_pendingItem.len - g_pendingOffset;
  if (remaining == 0u) {
    g_pendingValid = false;
    return true;
  }

  const uint32_t written =
      tuh_midi_stream_write(g_hostMidiIdx, g_hostMidiTxCable,
                            g_pendingItem.data + g_pendingOffset, remaining);
  if (written == 0u) {
    return false;
  }

  g_pendingOffset += static_cast<uint8_t>(written);
  if (g_pendingOffset >= g_pendingItem.len) {
    g_pendingValid = false;
    g_pendingOffset = 0;
  }

  return true;
}
} // namespace

void sendUSBMidiMessage(uint8_t const *midicmd, uint8_t len) {
  ScopedDWTProfile usbMidiQueueProfile(&gUsbMidiQueueProfile);
  if ((midicmd == nullptr) || (len == 0u)) {
    return;
  }

  if (!canQueueUsbMidiTx()) {
    return;
  }

  ensureUsbMidiTxQueue();
  if (g_usbMidiTxQueue == nullptr) {
    return;
  }

  uint8_t offset = 0;
  while (offset < len) {
    UsbMidiTxItem item{};
    item.len = ((len - offset) > kUsbMidiTxMaxMsgLen)
                   ? kUsbMidiTxMaxMsgLen
                   : static_cast<uint8_t>(len - offset);
    memcpy(item.data, midicmd + offset, item.len);

    if (xQueueSend(g_usbMidiTxQueue, &item, 0) != pdTRUE) {
      g_usbMidiTxDroppedItems++;
      if (!g_usbMidiTxDropLogged) {
        Trace::Error("USBMIDI: TX queue full, dropping MIDI data");
        g_usbMidiTxDropLogged = true;
      }
      break;
    }

    offset += item.len;
  }
}

void pumpUSBMidiTxQueue() {
  ensureUsbMidiTxQueue();
  if (g_usbMidiTxQueue == nullptr) {
    return;
  }

  const bool hostMode = tuh_inited();
  for (uint32_t i = 0; i < 32u; ++i) {
    if (!g_pendingValid) {
      if (xQueueReceive(g_usbMidiTxQueue, &g_pendingItem, 0) != pdTRUE) {
        break;
      }
      g_pendingOffset = 0;
      g_pendingValid = true;
    }

    const bool progressed =
        hostMode ? pumpNextItemHost() : pumpNextItemDevice();
    if (!progressed) {
      break;
    }
  }

  if ((uxQueueMessagesWaiting(g_usbMidiTxQueue) == 0u) && !g_pendingValid) {
    g_usbMidiTxDropLogged = false;
  }

  if (hostMode && g_hostMidiMounted && (g_hostMidiIdx != kInvalidHostMidiIdx) &&
      tuh_midi_mounted(g_hostMidiIdx)) {
    (void)tuh_midi_write_flush(g_hostMidiIdx);
  }
}

void handleUSBInterrupts() {
  updateUsbHostEnumerationTimeoutState();
  if (tuh_inited()) {
    tuh_task_ext(kUsbTaskTimeoutMs, false);
    return;
  }

  if (tud_inited()) {
    // Wait on TinyUSB's event queue, which wakes immediately from the USB
    // interrupt. A separate tick delay can miss a one-ms isochronous deadline.
    // The event budget still bounds each pass for MIDI and role maintenance.
    tud_task_ext(kUsbTaskTimeoutMs, false);
    usbMasterAudioService();
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(kUsbTaskTimeoutMs));
}

void resetUSBMidiSoftwareState() {
  usbMasterAudioSetConfigured(0);
  usbMasterAudioSetStreaming(0);
  usbMasterAudioReset();
  taskENTER_CRITICAL();
  if (g_usbMidiTxQueue != nullptr) {
    xQueueReset(g_usbMidiTxQueue);
  }

  resetHostMidiSelection();
  resetUsbHostIndicatorState();
  g_pendingItem = {};
  g_pendingOffset = 0;
  g_pendingValid = false;
  g_usbMidiTxDroppedItems = 0;
  g_usbMidiTxDropLogged = false;
  taskEXIT_CRITICAL();
}

extern "C" void tud_event_hook_cb(uint8_t rhport, uint32_t eventId,
                                  bool inIsr) {
  (void)rhport;
  (void)inIsr;
  // This hook may run in the interrupt. These operations only change atomic
  // state; FIFO and queue resets remain owned by the USB task.
  if (eventId == DCD_EVENT_BUS_RESET || eventId == DCD_EVENT_UNPLUGGED) {
    usbMasterAudioSetConfigured(0);
    usbMasterAudioSetStreaming(0);
    usbMasterAudioSetSuspended(0);
    usbMasterAudioReset();
  } else if (eventId == DCD_EVENT_SUSPEND) {
    usbMasterAudioSetSuspended(1);
  } else if (eventId == DCD_EVENT_RESUME) {
    // TinyUSB can skip the task callback while a connection is settling.
    usbMasterAudioSetSuspended(0);
  }
}

extern "C" void tud_mount_cb(void) {
  usbMasterAudioSetSuspended(0);
  usbMasterAudioSetConfigured(1);
}

extern "C" void tud_umount_cb(void) {
  usbMasterAudioSetConfigured(0);
  usbMasterAudioSetStreaming(0);
  usbMasterAudioReset();
}

extern "C" void tud_suspend_cb(bool remoteWakeupEnabled) {
  (void)remoteWakeupEnabled;
  usbMasterAudioSetSuspended(1);
}

extern "C" void tud_resume_cb(void) { usbMasterAudioSetSuspended(0); }

extern "C" void tuh_midi_mount_cb(uint8_t idx,
                                  const tuh_midi_mount_cb_t *mount_cb_data) {
  uint16_t vendorId = 0;
  uint16_t productId = 0;
  uint8_t interfaceNumber = 0xFF;
  if (mount_cb_data != nullptr) {
    interfaceNumber = mount_cb_data->bInterfaceNumber;
    (void)tuh_vid_pid_get(mount_cb_data->daddr, &vendorId, &productId);
  }

  selectHostMidiInterface(idx, interfaceNumber);
  if ((mount_cb_data != nullptr) && (idx == g_hostMidiIdx)) {
    g_hostMidiVendorId = vendorId;
    g_hostMidiProductId = productId;
    g_hostMidiIsApcMiniMk2 = isApcMiniMk2(vendorId, productId);
    configureHostMidiCables(mount_cb_data->rx_cable_count,
                            mount_cb_data->tx_cable_count);
    refreshControlSurfaceSelection();
    Trace::Log(
        "USBMIDI",
        "selected host midi idx=%u itf=%u rxCable=%u txCable=%u "
        "vid=%04x pid=%04x apc=%s",
        static_cast<unsigned>(idx), static_cast<unsigned>(interfaceNumber),
        static_cast<unsigned>(g_hostMidiRxCable),
        static_cast<unsigned>(g_hostMidiTxCable),
        static_cast<unsigned>(vendorId), static_cast<unsigned>(productId),
        g_hostMidiIsApcMiniMk2 ? "yes" : "no");
  }
}

extern "C" void tuh_mount_cb(uint8_t daddr) {
  Trace::Log("USBHOST", "tuh_mount_cb daddr=%u", static_cast<unsigned>(daddr));
  g_usbHostDeviceAttached = true;
  g_usbHostDeviceEnumerated = true;
  g_usbHostEnumerationTimedOut = false;
  g_usbHostStartupFailed = false;
  Trace::Log("USBHOST", "device enumerated successfully");
}

extern "C" void tuh_midi_umount_cb(uint8_t idx) {
  if (idx == g_hostMidiIdx) {
    resetHostMidiSelection();
    for (uint8_t otherIdx = 0; otherIdx < CFG_TUH_MIDI; ++otherIdx) {
      if ((otherIdx != idx) && tuh_midi_mounted(otherIdx)) {
        tuh_itf_info_t itfInfo{};
        uint8_t interfaceNumber = 0xFF;
        if (tuh_midi_itf_get_info(otherIdx, &itfInfo)) {
          interfaceNumber = itfInfo.desc.bInterfaceNumber;
        }
        selectHostMidiInterface(otherIdx, interfaceNumber);
        uint16_t vendorId = 0;
        uint16_t productId = 0;
        (void)tuh_vid_pid_get(itfInfo.daddr, &vendorId, &productId);
        g_hostMidiVendorId = vendorId;
        g_hostMidiProductId = productId;
        g_hostMidiIsApcMiniMk2 = isApcMiniMk2(vendorId, productId);
        configureHostMidiCables(tuh_midi_get_rx_cable_count(otherIdx),
                                tuh_midi_get_tx_cable_count(otherIdx));
        break;
      }
    }
    g_pendingValid = false;
    g_pendingOffset = 0;
    refreshControlSurfaceSelection();
  }
}

extern "C" void tuh_umount_cb(uint8_t daddr) {
  Trace::Log("USBHOST", "tuh_umount_cb daddr=%u", static_cast<unsigned>(daddr));
  noteUsbHostRemove();
}

extern "C" void tuh_event_hook_cb(uint8_t rhport, uint32_t eventid,
                                  bool in_isr) {
  if (eventid == HCD_EVENT_DEVICE_ATTACH) {
    noteUsbHostAttach(in_isr);
  } else if (eventid == HCD_EVENT_DEVICE_REMOVE) {
    noteUsbHostRemove();
  }
}

void suppressDwc2OtgInt() {
  USB_OTG_GlobalTypeDef *const global = USB_OTG_FS;
  global->GOTGINT = 0xFFFFFFFFu;
  global->GINTSTS = USB_OTG_GINTSTS_OTGINT | USB_OTG_GINTSTS_CIDSCHG;
  global->GINTMSK &= ~(USB_OTG_GINTMSK_OTGINT | USB_OTG_GINTMSK_CIDSCHGM);
}

extern "C" void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes) {
  (void)xferred_bytes;
  if (!tuh_midi_mounted(idx)) {
    return;
  }

  uint8_t packet[4];
  while (tuh_midi_packet_read(idx, packet)) {
    const uint8_t payloadLength = usbMidiPayloadLength(packet[0] & 0x0Fu);
    if (payloadLength == 0u) {
      continue;
    }

    advUSBMidiHostFeedRxBytes(&packet[1], payloadLength);
  }
}

bool usbHostMidiMounted() { return canQueueUsbMidiTx() && tuh_inited(); }

UsbHostControlSurfaceType usbHostControlSurfaceType() {
  if (!usbHostMidiMounted()) {
    return USB_HOST_CONTROL_SURFACE_NONE;
  }

  if (isApcMiniMk2(g_hostMidiVendorId, g_hostMidiProductId)) {
    return USB_HOST_CONTROL_SURFACE_APC_MINI_MK2;
  }

  if (isLaunchpadMiniMk3(g_hostMidiVendorId, g_hostMidiProductId)) {
    return USB_HOST_CONTROL_SURFACE_LAUNCHPAD_MINI_MK3;
  }

  return USB_HOST_CONTROL_SURFACE_NONE;
}

bool usbHostModeActive() { return tuh_inited(); }

bool usbHostDeviceEnumerated() {
  return tuh_inited() && g_usbHostDeviceEnumerated;
}

bool usbHostEnumerationTimedOut() {
  updateUsbHostEnumerationTimeoutState();
  return tuh_inited() && g_usbHostEnumerationTimedOut &&
         !g_usbHostDeviceEnumerated;
}

bool usbHostStartupFailed() { return g_usbHostStartupFailed; }

void usbHostSetStartupFailed(bool failed) { g_usbHostStartupFailed = failed; }

uint32_t usbHostMidiDroppedTxCount() { return g_usbMidiTxDroppedItems; }
