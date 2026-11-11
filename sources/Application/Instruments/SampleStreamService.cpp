/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SampleStreamService.h"
#include "System/Profiler/StreamingPerf.h"

#include "System/FileSystem/FileHandle.h"
#include "System/FileSystem/FileSystem.h"
#include <array>
#include <cstring>
#include <new>

#if defined(ADV)
#include "Application/Model/Song.h"
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
  uint32_t queuedCycles_;
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

// Eight streaming handles leave four slots for UI/import in the file pool.
// Cache lives in explicitly initialized D3 storage.
struct CachedFile {
  FileHandle file_;
  std::array<char, PFILENAME_SIZE> path_{};
  TickType_t usedAt_ = 0;
};

alignas(CachedFile) uint8_t
    fileCacheStorage[sizeof(std::array<CachedFile, SONG_CHANNEL_COUNT>)]
    __attribute__((section(".RAM_D3")));

void StreamWorker(void *) {
  // Placement construction explicitly initializes non-trivial handles in NOLOAD
  // D3 storage without using the heap or growing the task's stack frame.
  auto &cache =
      *new (fileCacheStorage) std::array<CachedFile, SONG_CHANNEL_COUNT>{};
  StreamRequest request{};
  while (true) {
    if (xQueueReceive(requestQueue, &request, pdMS_TO_TICKS(250)) != pdTRUE) {
      // Release idle assets so preview, deletion and recording can reclaim
      // the small file pool even when the sequencer has stopped.
      for (auto &entry : cache) {
        if (entry.file_ &&
            xTaskGetTickCount() - entry.usedAt_ >= pdMS_TO_TICKS(500)) {
          entry.file_.reset();
        }
      }
      continue;
    }
    if (request.bytes_ == 0) {
      for (auto &entry : cache) {
        entry.file_.reset();
      }
      complete(request, 2);
      continue;
    }
    StreamingPerfScope::Record(streamingPerf.streamQueueWait,
                               DWT->CYCCNT - request.queuedCycles_);
    StreamingPerfScope perf(streamingPerf.streamRead);
    CachedFile *selected = nullptr;
    for (auto &entry : cache) {
      if (entry.file_ && std::strcmp(entry.path_.data(), request.path_) == 0) {
        selected = &entry;
        streamingPerf.fileCacheHits = streamingPerf.fileCacheHits + 1;
        break;
      }
    }
    if (selected == nullptr) {
      for (auto &entry : cache) {
        if (!entry.file_) {
          selected = &entry;
          break;
        }
        if (selected == nullptr ||
            xTaskGetTickCount() - entry.usedAt_ >
                xTaskGetTickCount() - selected->usedAt_) {
          selected = &entry;
        }
      }
      StreamingPerfScope openPerf(streamingPerf.streamOpen);
      selected->file_.reset();
      selected->file_ = FileSystem::GetInstance()->Open(request.path_, "r");
      std::strcpy(selected->path_.data(), request.path_);
    }
    auto &file = selected->file_;
    selected->usedAt_ = xTaskGetTickCount();
    if (!file) {
      streamingPerf.streamErrors = streamingPerf.streamErrors + 1;
      complete(request, 3);
      continue;
    }
    if (file->Tell() != static_cast<long>(request.offset_)) {
      StreamingPerfScope seekPerf(streamingPerf.streamSeek);
      file->Seek(static_cast<long>(request.offset_), SEEK_SET);
    }
    bool success = file->Tell() == static_cast<long>(request.offset_) &&
                   file->Error() == 0;
    if (success) {
      StreamingPerfScope readPerf(streamingPerf.streamDataRead);
      const int32_t bytesRead =
          file->Read(request.destination_, request.bytes_);
      success =
          bytesRead >= 0 && static_cast<uint32_t>(bytesRead) == request.bytes_;
    }
    if (!success) {
      streamingPerf.streamErrors = streamingPerf.streamErrors + 1;
      file.reset();
    }
    complete(request, success ? 2 : 3);
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
  request.queuedCycles_ = DWT->CYCCNT;
  request.destination_ = destination;
  request.state_ = &state;
  request.pending_ = &pending;
  state.store(1, std::memory_order_relaxed);
  pending.fetch_add(1, std::memory_order_relaxed);
  // Read is called by the audio task and during preload. Serialize telemetry
  // with the enqueue so the worker cannot consume before measuring depth.
  taskENTER_CRITICAL();
  const BaseType_t queued = xQueueSend(requestQueue, &request, 0);
  const uint32_t depth = uxQueueMessagesWaiting(requestQueue);
  if (depth > streamingPerf.queueHighWater) {
    streamingPerf.queueHighWater = depth;
  }
  if (queued != pdTRUE) {
    streamingPerf.queueFull = streamingPerf.queueFull + 1;
  }
  taskEXIT_CRITICAL();
  if (queued != pdTRUE) {
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

void SampleStreamService::Release(std::atomic<uint16_t> &pending) {
  Wait(pending);
  if (requestQueue == nullptr) {
    return;
  }
  std::atomic<uint8_t> state{0};
  StreamRequest request{};
  request.state_ = &state;
  request.pending_ = &pending;
  pending.fetch_add(1, std::memory_order_relaxed);
  xQueueSend(requestQueue, &request, portMAX_DELAY);
  Wait(pending);
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

void SampleStreamService::Release(std::atomic<uint16_t> &pending) {
  Wait(pending);
}

#endif
