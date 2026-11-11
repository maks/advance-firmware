/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_FILESYSTEM_H
#define PICOTRACKER_WASM_FILESYSTEM_H

#include "System/FileSystem/FileSystem.h"

#include <cstdint>
#include <mutex>
#include <string>

class WasmFileSystem final : public FileSystem {
public:
  static constexpr const char *MountPoint = "/data";

  explicit WasmFileSystem(std::string mountPoint = MountPoint);

  FileHandle Open(const char *name, const char *mode) override;
  bool ListDirectory(const char *absoluteDir,
                     etl::ivector<FileSystemEntry> *entries, const char *filter,
                     bool directoriesOnly, bool includeHidden = false) override;
  bool DeleteFile(const char *name) override;
  bool DeleteDir(const char *name) override;
  bool exists(const char *path) override;
  bool makeDir(const char *path, bool pFlag = false) override;
  bool CopyFile(const char *srcFilename, const char *destFilename) override;
  bool MoveFile(const char *srcFilename, const char *destFilename) override;
  bool isExFat() override { return false; }

private:
  bool Resolve(const char *path, std::string &resolved) const;

  mutable std::recursive_mutex mutex_;
  std::string root_;
};

#endif
