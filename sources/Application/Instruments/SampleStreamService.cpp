/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SampleStreamService.h"

#include "System/FileSystem/FileHandle.h"
#include "System/FileSystem/FileSystem.h"
#include <cstring>

#if defined(ADV)
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

namespace {
constexpr UBaseType_t RequestCount = 16;
constexpr uint32_t WorkerStackWords = 2048;

struct StreamRequest {
  char path_[PFILENAME_SIZE];
  uint32_t offset_;
  uint32_t bytes_;
  void *destination_;
  std::atomic<uint8_t> *state_;
  std::atomic<uint16_t> *pending_;
};

struct StreamWorkerStorage {
  StaticQueue_t queueControl_{};
  uint8_t queueBytes_[RequestCount * sizeof(StreamRequest)]{};
  StaticTask_t taskControl_{};
  StackType_t taskStack_[WorkerStackWords]{};
};

StreamWorkerStorage workerStorage
    __attribute__((section(".RAM_D3"), aligned(8)));
QueueHandle_t requestQueue = nullptr;
TaskHandle_t workerTask = nullptr;

void complete(const StreamRequest &request, uint8_t state) {
  request.state_->store(state, std::memory_order_release);
  request.pending_->fetch_sub(1, std::memory_order_release);
}

void StreamWorker(void *) {
  StreamRequest request{};
  while (true) {
    if (xQueueReceive(requestQueue, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    FileHandle file = FileSystem::GetInstance()->Open(request.path_, "r");
    if (!file) {
      complete(request, 3);
      continue;
    }

    file->Seek(static_cast<long>(request.offset_), SEEK_SET);
    const int bytesRead = file->Read(request.destination_, request.bytes_);
    if (bytesRead < 0 || static_cast<uint32_t>(bytesRead) != request.bytes_) {
      complete(request, 3);
      continue;
    }
    complete(request, 2);
  }
}
} // namespace

bool SampleStreamService::Start() {
  if (workerTask != nullptr) {
    return true;
  }
  requestQueue = xQueueCreateStatic(RequestCount, sizeof(StreamRequest),
                                    workerStorage.queueBytes_,
                                    &workerStorage.queueControl_);
  if (requestQueue == nullptr) {
    return false;
  }
  workerTask = xTaskCreateStatic(StreamWorker, "SampleStream", WorkerStackWords,
                                 nullptr, 3, workerStorage.taskStack_,
                                 &workerStorage.taskControl_);
  return workerTask != nullptr;
}

bool SampleStreamService::Read(const char *path, uint32_t offset,
                               void *destination, uint32_t bytes,
                               std::atomic<uint8_t> &state,
                               std::atomic<uint16_t> &pending) {
  if (requestQueue == nullptr || path == nullptr || destination == nullptr ||
      bytes == 0 || std::strlen(path) >= PFILENAME_SIZE) {
    return false;
  }

  StreamRequest request{};
  std::strcpy(request.path_, path);
  request.offset_ = offset;
  request.bytes_ = bytes;
  request.destination_ = destination;
  request.state_ = &state;
  request.pending_ = &pending;
  state.store(1, std::memory_order_relaxed);
  pending.fetch_add(1, std::memory_order_relaxed);
  if (xQueueSend(requestQueue, &request, 0) != pdTRUE) {
    pending.fetch_sub(1, std::memory_order_relaxed);
    state.store(0, std::memory_order_release);
    return false;
  }
  return true;
}

void SampleStreamService::Wait(std::atomic<uint16_t> &pending) {
  while (pending.load(std::memory_order_acquire) != 0) {
    vTaskDelay(1);
  }
}

#else

bool SampleStreamService::Start() { return true; }

bool SampleStreamService::Read(const char *path, uint32_t offset,
                               void *destination, uint32_t bytes,
                               std::atomic<uint8_t> &state,
                               std::atomic<uint16_t> &pending) {
  state.store(1, std::memory_order_relaxed);
  pending.fetch_add(1, std::memory_order_relaxed);
  FileHandle file = FileSystem::GetInstance()->Open(path, "r");
  if (!file) {
    state.store(3, std::memory_order_release);
    pending.fetch_sub(1, std::memory_order_release);
    return false;
  }
  file->Seek(static_cast<long>(offset), SEEK_SET);
  const int bytesRead = file->Read(destination, bytes);
  if (bytesRead < 0 || static_cast<uint32_t>(bytesRead) != bytes) {
    state.store(3, std::memory_order_release);
    pending.fetch_sub(1, std::memory_order_release);
    return false;
  }
  state.store(2, std::memory_order_release);
  pending.fetch_sub(1, std::memory_order_release);
  return true;
}

void SampleStreamService::Wait(std::atomic<uint16_t> &pending) {
  (void)pending;
}

#endif
