/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#ifndef _FILESYSTEM_H_
#define _FILESYSTEM_H_

#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/T_Factory.h"
#include "System/FileSystem/FileHandle.h"
#include <cstddef>
#include <stdint.h>

#define MAX_FILE_INDEX_SIZE 256
#define PFILENAME_SIZE 256                 // per FAT32 spec for LFNs
#define MAX_PROJECT_SAMPLE_PATH_LENGTH 146 // 17 + 128 + 1

enum PicoFileType { PFT_UNKNOWN, PFT_FILE, PFT_DIR };

enum class DirectoryListStatus : uint8_t {
  Ok = 0,
  Error,
  Truncated,
};

struct FileSystemEntry {
  etl::string<PFILENAME_SIZE> name;
  PicoFileType type = PFT_UNKNOWN;
  uint64_t size = 0;
  bool isHidden = false;
};

// Forward declaration
class I_File;

// This is the main FileSystem interface that will be implemented by
// platform-specific classes
class FileSystem : public T_Factory<FileSystem> {
public:
  FileSystem() {}
  virtual ~FileSystem() {}

  virtual FileHandle Open(const char *name, const char *mode) = 0;
  virtual bool read(int index, void *data) {
    return false;
  } // Default implementation
  virtual bool ListDirectory(const char *absoluteDir,
                             etl::ivector<FileSystemEntry> *entries,
                             const char *filter, bool directoriesOnly,
                             bool includeHidden = false) = 0;
  virtual bool ListDirectoryPage(const char *absoluteDir,
                                 etl::ivector<FileSystemEntry> *entries,
                                 const char *filter, bool directoriesOnly,
                                 bool includeHidden, size_t offset,
                                 size_t &totalMatches) {
    if (offset != 0) {
      totalMatches = 0;
      return false;
    }
    const bool result = ListDirectory(absoluteDir, entries, filter,
                                      directoriesOnly, includeHidden);
    totalMatches = entries == nullptr ? 0 : entries->size();
    return result;
  }
  virtual bool DeleteFile(const char *name) = 0;
  virtual bool DeleteDir(const char *name) = 0;
  // Optional batching hook for filesystem implementations that cache listings.
  virtual void BeginBatch() {}
  virtual void EndBatch() {}
  virtual bool exists(const char *path) = 0;
  virtual bool makeDir(const char *path, bool pFlag = false) = 0;
  virtual bool CopyFile(const char *srcFilename, const char *destFilename) = 0;
  virtual bool MoveFile(const char *srcFilename, const char *destFilename) = 0;
  // Returns available bytes on the volume containing path. False means the
  // filesystem cannot establish a reliable capacity estimate.
  virtual bool GetFreeSpaceBytes(const char *path, uint64_t &bytes) {
    (void)path;
    bytes = 0;
    return false;
  }
  // Rename a directory without replacing an existing destination.
  virtual bool RenameDir(const char *srcPath, const char *destPath) {
    (void)srcPath;
    (void)destPath;
    return false;
  }
  virtual bool isExFat() = 0;
  DirectoryListStatus GetLastListDirectoryStatus() const {
    return lastListDirectoryStatus_;
  }

protected:
  void SetLastListDirectoryStatus(DirectoryListStatus status) {
    lastListDirectoryStatus_ = status;
  }

private:
  DirectoryListStatus lastListDirectoryStatus_ = DirectoryListStatus::Ok;
};

#endif // _FILESYSTEM_H_
