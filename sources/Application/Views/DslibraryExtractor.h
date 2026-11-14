/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef PICOTRACKER_DSLIBRARY_EXTRACTOR_H
#define PICOTRACKER_DSLIBRARY_EXTRACTOR_H

#include "Externals/etl/include/etl/string.h"
#include "System/FileSystem/FileSystem.h"
#include <cstdint>

class DslibraryExtractor {
public:
  enum class Result : uint8_t {
    Success,
    Cancelled,
    ExistingDestination,
    InvalidArchive,
    UnsupportedArchive,
    InvalidPath,
    InsufficientSpace,
    FilesystemError,
    OutOfMemory
  };
  struct Progress {
    const char *stage;
    const char *filename;
    uint64_t extractedBytes;
    uint64_t totalBytes;
  };
  using ProgressCallback = bool (*)(const Progress &, void *);

  static bool IsActive();
  static bool StartAsync(const char *archivePath, const char *destinationPath);
  static void Cancel();
  static bool GetAsyncProgress(Progress &progress, char *stage,
                               size_t stageCapacity, char *filename,
                               size_t filenameCapacity);
  static bool TakeAsyncResult(Result &result);
  static uint32_t GetPresetSummary(char *firstPath, size_t pathCapacity);
  static bool CleanupIncompleteStage(const char *stagingPath);
  static Result Extract(const char *archivePath, const char *destinationPath,
                        ProgressCallback callback, void *context);
};

#endif
