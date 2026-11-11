/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "advUsbRoleManager.h"
#include "Adapters/adv/system/i2cTransfer.h"
#include "System/Console/Trace.h"
#include "main.h"
#include "tim.h"

namespace {
constexpr uint16_t kFusb303I2cAddress = 0x21u << 1;

constexpr uint8_t kFusb303DeviceIdReg = 0x01u;
constexpr uint8_t kFusb303DeviceTypeReg = 0x02u;
constexpr uint8_t kFusb303PortRoleReg = 0x03u;
constexpr uint8_t kFusb303ControlReg = 0x04u;
constexpr uint8_t kFusb303Control1Reg = 0x05u;
constexpr uint8_t kFusb303MaskReg = 0x0Eu;
constexpr uint8_t kFusb303Mask1Reg = 0x0Fu;
constexpr uint8_t kFusb303ResetReg = 0x0Au;
constexpr uint8_t kFusb303StatusReg = 0x11u;
constexpr uint8_t kFusb303Status1Reg = 0x12u;
constexpr uint8_t kFusb303TypeReg = 0x13u;
constexpr uint8_t kFusb303InterruptReg = 0x14u;
constexpr uint8_t kFusb303Interrupt1Reg = 0x15u;

constexpr uint8_t kFusb303PortRoleTryMask = 0x30u;
constexpr uint8_t kFusb303PortRoleTrySnkValue = 0x10u;

constexpr uint8_t kFusb303ControlRuntimeValue = 0x42u;
constexpr uint8_t kFusb303Control1RuntimeValue = 0xAEu;
constexpr uint8_t kFusb303UnmaskAllInterruptsValue = 0x00u;
constexpr uint8_t kFusb303ResetValue = 0x01u;

constexpr uint8_t kFusb303StatusAttachBit = 1u << 0;
constexpr uint8_t kFusb303StatusVbusOkBit = 1u << 3;
constexpr uint8_t kFusb303TypeSourceBit = 1u << 3;
constexpr uint8_t kFusb303TypeSinkBit = 1u << 4;

constexpr uint8_t kFusb303Status1FaultBit = 1u << 1;
constexpr uint8_t kFusb303Status1RemedyBit = 1u << 0;

constexpr uint8_t kFusb303InterruptFaultBit = 1u << 5;
constexpr uint8_t kFusb303Interrupt1RemedyFailBit = 1u << 3;
constexpr uint8_t kFusb303Interrupt1RemedyBit = 1u << 0;

constexpr uint32_t kTim2TicksPerMillisecond = 1000u;

struct AdvUsbRoleManagerState {
  bool ready;
  AdvUsbTypeCStatus status;
};

AdvUsbRoleManagerState g_state = {
    .ready = false,
    .status =
        {
            .deviceId = 0,
            .deviceType = 0,
            .portRole = 0,
            .control = 0,
            .control1 = 0,
            .mask = 0,
            .mask1 = 0,
            .status = 0,
            .status1 = 0,
            .type = 0,
            .interrupt = 0,
            .interrupt1 = 0,
            .attached = false,
            .source = false,
            .sink = false,
            .vbusOk = false,
            .fault = false,
            .remedy = false,
            .orient = ADV_USB_TYPEC_ORIENT_NONE,
            .cableState = ADV_USB_CABLE_DETACHED,
            .interruptCause = 0,
        },
};

bool readRegister(uint8_t reg, uint8_t &value) {
  const HAL_StatusTypeDef status =
      AdvI2C_Mem_Read(&hi2c4, kFusb303I2cAddress, reg, I2C_MEMADD_SIZE_8BIT,
                      &value, 1, I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("USBROLE: i2c read failed reg=0x%02x status=%u error=0x%08lx",
                 reg, static_cast<uint32_t>(status),
                 static_cast<unsigned long>(HAL_I2C_GetError(&hi2c4)));
    return false;
  }

  return true;
}

bool writeRegister(uint8_t reg, uint8_t value) {
  const HAL_StatusTypeDef status =
      AdvI2C_Mem_Write(&hi2c4, kFusb303I2cAddress, reg, I2C_MEMADD_SIZE_8BIT,
                       &value, 1, I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("USBROLE: i2c write failed reg=0x%02x value=0x%02x status=%u "
                 "error=0x%08lx",
                 reg, value, static_cast<uint32_t>(status),
                 static_cast<unsigned long>(HAL_I2C_GetError(&hi2c4)));
    return false;
  }

  return true;
}

void bootDelayMs(uint32_t delayMs) {
  const uint32_t start = __HAL_TIM_GET_COUNTER(&htim2);
  const uint32_t ticksToWait = delayMs * kTim2TicksPerMillisecond;
  while ((__HAL_TIM_GET_COUNTER(&htim2) - start) < ticksToWait) {
  }
}

const char *cableStateToString(AdvUsbCableState state) {
  switch (state) {
  case ADV_USB_CABLE_DETACHED:
    return "detached";
  case ADV_USB_CABLE_DEVICE:
    return "device";
  case ADV_USB_CABLE_HOST:
    return "host";
  case ADV_USB_CABLE_UNRESOLVED:
  default:
    return "unresolved";
  }
}

const char *orientationToString(AdvUsbTypeCOrientation orient) {
  switch (orient) {
  case ADV_USB_TYPEC_ORIENT_CC1:
    return "cc1";
  case ADV_USB_TYPEC_ORIENT_CC2:
    return "cc2";
  case ADV_USB_TYPEC_ORIENT_FAULT:
    return "fault";
  case ADV_USB_TYPEC_ORIENT_NONE:
  default:
    return "none";
  }
}

AdvUsbTypeCOrientation decodeOrientation(uint8_t status) {
  switch ((status >> 4) & 0x03u) {
  case 0x01u:
    return ADV_USB_TYPEC_ORIENT_CC1;
  case 0x02u:
    return ADV_USB_TYPEC_ORIENT_CC2;
  case 0x03u:
    return ADV_USB_TYPEC_ORIENT_FAULT;
  default:
    return ADV_USB_TYPEC_ORIENT_NONE;
  }
}

AdvUsbCableState decodeCableState(const AdvUsbTypeCStatus &status) {
  if (!status.attached) {
    return ADV_USB_CABLE_DETACHED;
  }

  if (status.source && !status.sink) {
    return ADV_USB_CABLE_HOST;
  }

  if (status.sink && !status.source) {
    return ADV_USB_CABLE_DEVICE;
  }

  return ADV_USB_CABLE_UNRESOLVED;
}

void updateDecodedFields(AdvUsbTypeCStatus &status) {
  status.attached = (status.status & kFusb303StatusAttachBit) != 0u;
  status.vbusOk = (status.status & kFusb303StatusVbusOkBit) != 0u;
  status.source = (status.type & kFusb303TypeSourceBit) != 0u;
  status.sink = (status.type & kFusb303TypeSinkBit) != 0u;
  status.fault = (status.status1 & kFusb303Status1FaultBit) != 0u;
  status.remedy = (status.status1 & kFusb303Status1RemedyBit) != 0u;
  status.orient = decodeOrientation(status.status);
  status.cableState = decodeCableState(status);
}

bool readTypeCRegisters(AdvUsbTypeCStatus &status) {
  if (!readRegister(kFusb303StatusReg, status.status)) {
    return false;
  }
  if (!readRegister(kFusb303Status1Reg, status.status1)) {
    return false;
  }
  if (!readRegister(kFusb303TypeReg, status.type)) {
    return false;
  }

  updateDecodedFields(status);
  return true;
}

bool clearInterruptRegisters(AdvUsbTypeCStatus &status) {
  uint8_t interrupt = 0;
  uint8_t interrupt1 = 0;

  if (!readRegister(kFusb303InterruptReg, interrupt)) {
    return false;
  }
  if (!readRegister(kFusb303Interrupt1Reg, interrupt1)) {
    return false;
  }

  status.interrupt = interrupt;
  status.interrupt1 = interrupt1;
  status.interruptCause = static_cast<uint16_t>(interrupt) |
                          (static_cast<uint16_t>(interrupt1) << 8u);

  if ((interrupt != 0u) && !writeRegister(kFusb303InterruptReg, interrupt)) {
    return false;
  }
  if ((interrupt1 != 0u) && !writeRegister(kFusb303Interrupt1Reg, interrupt1)) {
    return false;
  }

  return true;
}

void traceStateChange(const AdvUsbTypeCStatus &previous,
                      const AdvUsbTypeCStatus &next, const char *reason) {
  if (previous.attached == next.attached && previous.source == next.source &&
      previous.sink == next.sink && previous.orient == next.orient &&
      previous.cableState == next.cableState && previous.fault == next.fault &&
      previous.remedy == next.remedy && next.interruptCause == 0) {
    return;
  }

  const char *errorRec = "";
  if ((next.interrupt & kFusb303InterruptFaultBit) != 0u) {
    errorRec = " [FAULT_INT]";
  } else if ((next.interrupt1 & kFusb303Interrupt1RemedyFailBit) != 0u) {
    errorRec = " [REM_FAIL_INT]";
  } else if (next.fault) {
    errorRec = " [FAULT_STAT]";
  }

  Trace::Debug("%s state=%s attach=%u source=%u sink=%u orient=%s fault=%u "
               "remedy=%u type=0x%02x status=0x%02x cause=0x%04x%s",
               reason, cableStateToString(next.cableState),
               next.attached ? 1u : 0u, next.source ? 1u : 0u,
               next.sink ? 1u : 0u, orientationToString(next.orient),
               next.fault ? 1u : 0u, next.remedy ? 1u : 0u, next.type,
               next.status, next.interruptCause, errorRec);
}

bool refreshStatus(const char *reason) {
  const AdvUsbTypeCStatus previousStatus = g_state.status;
  AdvUsbTypeCStatus nextStatus = g_state.status;

  if (!readTypeCRegisters(nextStatus)) {
    return false;
  }

  g_state.status = nextStatus;
  traceStateChange(previousStatus, nextStatus, reason);
  return true;
}
} // namespace

bool advUsbRoleManagerInit(void) {
  if (g_state.ready) {
    return true;
  }

  if (!writeRegister(kFusb303ResetReg, kFusb303ResetValue)) {
    Trace::Error("USBROLE: failed to reset FUSB303");
    return false;
  }
  bootDelayMs(10);

  uint8_t portRole = 0;
  if (!readRegister(kFusb303PortRoleReg, portRole)) {
    Trace::Error("USBROLE: failed to read FUSB303 port role for preference");
    return false;
  }
  portRole = static_cast<uint8_t>(
      (portRole & static_cast<uint8_t>(~kFusb303PortRoleTryMask)) |
      kFusb303PortRoleTrySnkValue);
  if (!writeRegister(kFusb303PortRoleReg, portRole)) {
    Trace::Error("USBROLE: failed to prefer FUSB303 sink role");
    return false;
  }

  if (!writeRegister(kFusb303ControlReg, kFusb303ControlRuntimeValue)) {
    Trace::Error("USBROLE: failed to program FUSB303 control register");
    return false;
  }
  if (!writeRegister(kFusb303Control1Reg, kFusb303Control1RuntimeValue)) {
    Trace::Error("USBROLE: failed to program FUSB303 control1 register");
    return false;
  }
  if (!writeRegister(kFusb303MaskReg, kFusb303UnmaskAllInterruptsValue)) {
    Trace::Error("USBROLE: failed to program FUSB303 mask register");
    return false;
  }
  if (!writeRegister(kFusb303Mask1Reg, kFusb303UnmaskAllInterruptsValue)) {
    Trace::Error("USBROLE: failed to program FUSB303 mask1 register");
    return false;
  }

  if (!readRegister(kFusb303DeviceIdReg, g_state.status.deviceId)) {
    Trace::Error("USBROLE: failed to read FUSB303 device id");
    return false;
  }
  if (!readRegister(kFusb303DeviceTypeReg, g_state.status.deviceType)) {
    Trace::Error("USBROLE: failed to read FUSB303 device type");
    return false;
  }
  if (!readRegister(kFusb303PortRoleReg, g_state.status.portRole)) {
    Trace::Error("USBROLE: failed to read FUSB303 port role");
    return false;
  }
  if (!readRegister(kFusb303ControlReg, g_state.status.control)) {
    Trace::Error("USBROLE: failed to read FUSB303 control");
    return false;
  }
  if (!readRegister(kFusb303Control1Reg, g_state.status.control1)) {
    Trace::Error("USBROLE: failed to read FUSB303 control1");
    return false;
  }
  if (!readRegister(kFusb303MaskReg, g_state.status.mask)) {
    Trace::Error("USBROLE: failed to read FUSB303 mask");
    return false;
  }
  if (!readRegister(kFusb303Mask1Reg, g_state.status.mask1)) {
    Trace::Error("USBROLE: failed to read FUSB303 mask1");
    return false;
  }

  if (!clearInterruptRegisters(g_state.status)) {
    Trace::Error("USBROLE: failed to clear FUSB303 interrupts during init");
    return false;
  }
  if (!refreshStatus("init")) {
    return false;
  }

  g_state.ready = true;
  Trace::Log(
      "USBROLE",
      "init device_id=0x%02x device_type=0x%02x portrole=0x%02x control=0x%02x "
      "control1=0x%02x mask=0x%02x mask1=0x%02x",
      g_state.status.deviceId, g_state.status.deviceType,
      g_state.status.portRole, g_state.status.control, g_state.status.control1,
      g_state.status.mask, g_state.status.mask1);
  return true;
}

void advUsbRoleManagerService(void) {
  if (!g_state.ready) {
    return;
  }

  if (!clearInterruptRegisters(g_state.status)) {
    return;
  }
  (void)refreshStatus("interrupt");
}

bool advUsbRoleManagerIsReady(void) { return g_state.ready; }

const AdvUsbTypeCStatus *advUsbRoleManagerGetStatus(void) {
  return &g_state.status;
}
