/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_SAMPLE_STREAM_SERVICE_H
#define PICOTRACKER_SAMPLE_STREAM_SERVICE_H

#include <atomic>
#include <cstdint>

class SampleStreamService {
public:
  static bool Start();
  static bool Read(const char *path, uint32_t offset, void *destination,
                   uint32_t bytes, std::atomic<uint8_t> &state,
                   std::atomic<uint16_t> &pending);
  static void Wait(std::atomic<uint16_t> &pending);
  // Called from lifecycle/UI code only, after stopping new requests.
  static void Release(std::atomic<uint16_t> &pending);
};

#endif
