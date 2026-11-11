/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>

class InputMap final {
public:
  static constexpr std::uint16_t ActionCount = 11;
  static constexpr int ActionEventCode = 2;
  using QueueActionFunction = bool (*)(std::uint16_t action, bool pressed);

  static bool SetAction(std::uint16_t action, bool pressed);
  static bool RepeatAction(std::uint16_t action);
  static void ReleaseAllActions();
  static void RetryPendingTransitions();
  static std::uint16_t GetHeldActionMask();
  static std::uint16_t GetDispatchedActionMask();
  static std::uint32_t GetDispatchGeneration();
  static std::uint16_t GetLastDispatchedAction();
  static void AcknowledgeAction(std::uint16_t action, bool pressed);
  static bool DecodeActionEvent(std::uintptr_t encoded, std::uint16_t &action,
                                bool &pressed);

  static void SetQueueForTesting(QueueActionFunction queue);
  static void ResetQueueForTesting();
};
