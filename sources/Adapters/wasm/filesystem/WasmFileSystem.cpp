/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmFileSystem.h"
#include "Adapters/wasm/tracing/WasmProfiler.h"
#include "Externals/etl/include/etl/string_utilities.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/PathUtils.h"
#include "WasmFile.h"
#include "WasmStorageBridge.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {
bool matchesFilter(const FileSystemPath::PathString &entryName,
                   const FileSystemPath::PathString &filter) {
  if (filter.empty()) {
    return true;
  }
  if (entryName.empty()) {
    return false;
  }

  FileSystemPath::PathString normalizedName(entryName);
  FileSystemPath::PathString normalizedFilter(filter);

  etl::to_lower_case(normalizedName);
  etl::to_lower_case(normalizedFilter);

  return normalizedName.find(normalizedFilter.c_str()) !=
         FileSystemPath::PathString::npos;
}

bool IsWithin(const fs::path &root, const fs::path &candidate) {
  auto rootPart = root.begin();
  auto candidatePart = candidate.begin();
  for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
    if (candidatePart == candidate.end() || *candidatePart != *rootPart) {
      return false;
    }
  }
  return true;
}
} // namespace

WasmFileSystem::WasmFileSystem(std::string mountPoint)
    : root_(fs::path(std::move(mountPoint)).lexically_normal().string()) {
  std::error_code error;
  fs::create_directories(root_, error);
  if (error) {
    Trace::Error("WASM_FILESYSTEM", "cannot create %s: %s", root_.c_str(),
                 error.message().c_str());
    return;
  }
  const fs::path canonicalRoot = fs::weakly_canonical(root_, error);
  if (error) {
    Trace::Error("WASM_FILESYSTEM", "cannot resolve %s: %s", root_.c_str(),
                 error.message().c_str());
    return;
  }
  root_ = canonicalRoot.string();
}

bool WasmFileSystem::Resolve(const char *path, std::string &resolved) const {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const fs::path rootPath(root_);
  fs::path requested(path);
  fs::path combined;
  if (requested.is_absolute()) {
    const fs::path normalizedReq = requested.lexically_normal();
    if (IsWithin(rootPath, normalizedReq)) {
      combined = normalizedReq;
    } else {
      combined = rootPath / requested.relative_path();
    }
  } else {
    combined = rootPath / requested;
  }

  const fs::path normalized = combined.lexically_normal();
  if (!IsWithin(rootPath, normalized)) {
    return false;
  }
  resolved = normalized.string();
  return true;
}

FileHandle WasmFileSystem::Open(const char *name, const char *mode) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(name, resolved)) {
    Trace::Error("WASM_FILESYSTEM", "failed to resolve: %s",
                 name ? name : "(null)");
    return FileHandle();
  }

  if (mode == nullptr || mode[0] == '\0') {
    return FileHandle();
  }

  if (mode[0] == 'w' || mode[0] == 'a') {
    std::error_code ec;
    fs::path parent = fs::path(resolved).parent_path();
    if (!parent.empty()) {
      fs::create_directories(parent, ec);
    }
  }

  std::FILE *fp = std::fopen(resolved.c_str(), mode);
  if (fp == nullptr) {
    return FileHandle();
  }

  const bool initiallyDirty = (mode[0] == 'w' || mode[0] == 'a');
  return FileHandle(new WasmFile(fp, initiallyDirty));
}

bool WasmFileSystem::ListDirectory(const char *absoluteDir,
                                   etl::ivector<FileSystemEntry> *entries,
                                   const char *filter, bool directoriesOnly,
                                   bool includeHidden) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (entries == nullptr) {
    SetLastListDirectoryStatus(DirectoryListStatus::Error);
    return false;
  }

  std::string resolved;
  if (!Resolve(absoluteDir, resolved)) {
    SetLastListDirectoryStatus(DirectoryListStatus::Error);
    return false;
  }

  std::error_code ec;
  if (!fs::exists(resolved, ec) || !fs::is_directory(resolved, ec)) {
    SetLastListDirectoryStatus(DirectoryListStatus::Error);
    return false;
  }

  FileSystemPath::PathString normalizedFilter;
  if (filter != nullptr) {
    normalizedFilter = filter;
  }

  bool success = true;
  SetLastListDirectoryStatus(DirectoryListStatus::Ok);

  for (const auto &entry : fs::directory_iterator(resolved, ec)) {
    if (ec) {
      SetLastListDirectoryStatus(DirectoryListStatus::Error);
      return false;
    }
    const std::string filename = entry.path().filename().string();
    if (filename == "." || filename == "..") {
      continue;
    }

    const bool isDir = entry.is_directory(ec);
    const bool isHidden = (!filename.empty() && filename[0] == '.');
    if (!includeHidden && isHidden) {
      continue;
    }
    if (directoriesOnly && !isDir) {
      continue;
    }

    FileSystemPath::PathString entryName(filename.c_str());
    if (entryName.is_truncated()) {
      SetLastListDirectoryStatus(DirectoryListStatus::Error);
      return false;
    }
    if (!isDir && !matchesFilter(entryName, normalizedFilter)) {
      continue;
    }

    if (entries->full()) {
      SetLastListDirectoryStatus(DirectoryListStatus::Truncated);
      success = false;
      break;
    }

    FileSystemEntry fe;
    fe.name = entryName;
    fe.type = isDir ? PFT_DIR : PFT_FILE;
    fe.size = isDir ? 0 : entry.file_size(ec);
    fe.isHidden = isHidden;
    entries->push_back(fe);
  }

  return success;
}

bool WasmFileSystem::DeleteFile(const char *name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(name, resolved)) {
    return false;
  }
  std::error_code ec;
  if (!fs::is_regular_file(resolved, ec)) {
    return false;
  }
  const bool removed = fs::remove(resolved, ec);
  if (removed) {
    WasmStorage_NotifyMutation();
  }
  return removed;
}

bool WasmFileSystem::DeleteDir(const char *name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(name, resolved)) {
    return false;
  }
  std::error_code ec;
  if (!fs::is_directory(resolved, ec)) {
    return false;
  }
  const bool removed = fs::remove(resolved, ec);
  if (removed) {
    WasmStorage_NotifyMutation();
  }
  return removed;
}

bool WasmFileSystem::exists(const char *path) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(path, resolved)) {
    return false;
  }
  std::error_code ec;
  return fs::exists(resolved, ec);
}

bool WasmFileSystem::makeDir(const char *path, bool pFlag) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(path, resolved)) {
    return false;
  }
  std::error_code ec;
  bool created = false;
  if (pFlag) {
    created = fs::create_directories(resolved, ec);
  } else {
    created = fs::create_directory(resolved, ec);
  }
  if (created) {
    WasmStorage_NotifyMutation();
  }
  return created || fs::exists(resolved, ec);
}

bool WasmFileSystem::CopyFile(const char *srcFilename,
                              const char *destFilename) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string srcResolved, destResolved;
  if (!Resolve(srcFilename, srcResolved) ||
      !Resolve(destFilename, destResolved)) {
    return false;
  }
  std::error_code ec;
  fs::path parent = fs::path(destResolved).parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent, ec);
  }
  const bool copied = fs::copy_file(srcResolved, destResolved,
                                    fs::copy_options::overwrite_existing, ec);
  if (copied) {
    WasmStorage_NotifyMutation();
  }
  return copied;
}

bool WasmFileSystem::MoveFile(const char *srcFilename,
                              const char *destFilename) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string srcResolved, destResolved;
  if (!Resolve(srcFilename, srcResolved) ||
      !Resolve(destFilename, destResolved)) {
    return false;
  }
  std::error_code ec;
  fs::path parent = fs::path(destResolved).parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent, ec);
  }
  fs::rename(srcResolved, destResolved, ec);
  if (!ec) {
    WasmStorage_NotifyMutation();
    return true;
  }
  return false;
}
