/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Application/Instruments/SamplePool.h"

#include <array>
#include <cstdint>

// Browser samples live in the WebAssembly heap for the lifetime of the loaded
// project. MEMFS retains the source files; the player reads the decoded sample
// data from these stable heap allocations.
class TestStreamingSamplePool final : public SamplePool {
public:
  TestStreamingSamplePool() = default;
  ~TestStreamingSamplePool() override;

  void Reset() override;
  bool CheckSampleFits(std::uint32_t sampleSize) override;
  std::uint32_t GetAvailableSampleStorageSpace() override;
  void *ReserveStreamingStorage(std::uint32_t bytes) override;
  void ReleaseStreamingStorage(void *memory) override;

protected:
  bool loadSample(const char *name) override;
  bool unloadSample(std::uint32_t index) override;

private:
  void ReleaseSample(std::uint32_t index);

  std::array<std::uint8_t *, MAX_SAMPLES> buffers_{};
  static constexpr std::size_t MaxStreamingReservations = 64;
  struct StreamingReservation {
    std::uint8_t *memory_ = nullptr;
    std::uint32_t size_ = 0;
  };
  std::array<StreamingReservation, MaxStreamingReservations> streaming_{};
};
