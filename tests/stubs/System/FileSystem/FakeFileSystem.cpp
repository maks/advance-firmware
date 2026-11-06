/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#include "System/FileSystem/FakeFileSystem.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace {

bool startsWith(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

std::string toLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
  return value;
}

} // namespace

class FakeFileSystem::FakeFile : public I_File {
public:
  FakeFile(FakeFileSystem *fs, std::string path, bool canRead, bool canWrite)
      : fs_(fs), path_(std::move(path)), canRead_(canRead), canWrite_(canWrite),
        cursor_(0), error_(0), isOpen_(true) {}

  int Read(void *ptr, int size) override {
    if (!isOpen_ || !canRead_ || ptr == nullptr || size < 0) {
      error_ = 1;
      return -1;
    }
    auto it = fs_->nodes_.find(path_);
    if (it == fs_->nodes_.end() || it->second.isDirectory) {
      error_ = 1;
      return -1;
    }
    const std::string &contents = it->second.contents;
    const size_t remaining =
        cursor_ < contents.size() ? (contents.size() - cursor_) : 0;
    const size_t bytesToRead = std::min(remaining, static_cast<size_t>(size));
    if (bytesToRead == 0) {
      return 0;
    }
    std::memcpy(ptr, contents.data() + cursor_, bytesToRead);
    cursor_ += bytesToRead;
    return static_cast<int>(bytesToRead);
  }

  int GetC() override {
    unsigned char ch = 0;
    const int read = Read(&ch, 1);
    if (read != 1) {
      return EOF;
    }
    return ch;
  }

  int Write(const void *ptr, int size, int nmemb) override {
    if (!isOpen_ || !canWrite_ || ptr == nullptr || size < 0 || nmemb < 0) {
      error_ = 1;
      return -1;
    }
    const size_t bytesToWrite =
        static_cast<size_t>(size) * static_cast<size_t>(nmemb);
    auto it = fs_->nodes_.find(path_);
    if (it == fs_->nodes_.end() || it->second.isDirectory) {
      error_ = 1;
      return -1;
    }
    std::string &contents = it->second.contents;
    if (cursor_ > contents.size()) {
      contents.resize(cursor_, '\0');
    }
    if ((cursor_ + bytesToWrite) > contents.size()) {
      contents.resize(cursor_ + bytesToWrite, '\0');
    }
    std::memcpy(contents.data() + cursor_, ptr, bytesToWrite);
    cursor_ += bytesToWrite;
    return static_cast<int>(bytesToWrite);
  }

  void Seek(long offset, int whence) override {
    auto it = fs_->nodes_.find(path_);
    if (!isOpen_ || it == fs_->nodes_.end() || it->second.isDirectory) {
      error_ = 1;
      return;
    }

    size_t target = 0;
    switch (whence) {
    case SEEK_SET:
      if (offset < 0) {
        error_ = 1;
        return;
      }
      target = static_cast<size_t>(offset);
      break;
    case SEEK_CUR:
      if (offset < 0 && static_cast<size_t>(-offset) > cursor_) {
        error_ = 1;
        return;
      }
      target = offset < 0 ? cursor_ - static_cast<size_t>(-offset)
                          : cursor_ + static_cast<size_t>(offset);
      break;
    case SEEK_END: {
      const size_t end = it->second.contents.size();
      if (offset < 0 && static_cast<size_t>(-offset) > end) {
        error_ = 1;
        return;
      }
      target = offset < 0 ? end - static_cast<size_t>(-offset)
                          : end + static_cast<size_t>(offset);
      break;
    }
    default:
      error_ = 1;
      return;
    }

    cursor_ = target;
  }

  long Tell() override { return static_cast<long>(cursor_); }

  int Error() override { return error_; }

  bool Sync() override { return isOpen_; }

  void Dispose() override { delete this; }

protected:
  bool Close() override {
    isOpen_ = false;
    return true;
  }

private:
  FakeFileSystem *fs_;
  std::string path_;
  bool canRead_;
  bool canWrite_;
  size_t cursor_;
  int error_;
  bool isOpen_;
};

FakeFileSystem::FakeFileSystem()
    : currentDirectory_("/"), allowLegacyCwdResolution_(false) {
  nodes_.emplace("/", Node{.isDirectory = true, .contents = ""});
}

FileHandle FakeFileSystem::Open(const char *name, const char *mode) {
  std::string resolvedPath;
  if (name != nullptr && allowLegacyCwdResolution_ &&
      !FileSystemPath::IsAbsolutePath(name)) {
    resolveRelativePath(currentDirectory_.c_str(), name, resolvedPath);
  } else {
    normalizeAbsolutePath(name, resolvedPath);
  }

  bool success = false;
  FileHandle handle;
  if (!shouldFail(FakeFileSystemOperationType::Open) && !resolvedPath.empty() &&
      mode != nullptr && mode[0] != '\0') {
    auto parent = std::string();
    if ((mode[0] == 'r') && nodes_.contains(resolvedPath) &&
        !nodes_.at(resolvedPath).isDirectory) {
      handle = MakeFileHandle(new FakeFile(this, resolvedPath, true, false));
      success = true;
    } else if ((mode[0] == 'w') && parentDirectory(resolvedPath, parent) &&
               ensureDirectoryExists(parent)) {
      nodes_[resolvedPath] = Node{.isDirectory = false, .contents = ""};
      handle = MakeFileHandle(new FakeFile(this, resolvedPath, false, true));
      success = true;
    }
  }

  logOperation(FakeFileSystemOperationType::Open, name ? name : "", "",
               mode ? mode : "", resolvedPath, success);
  return handle;
}

bool FakeFileSystem::chdir(const char *path) {
  std::string normalizedPath;
  const bool success = !shouldFail(FakeFileSystemOperationType::Chdir) &&
                       normalizeAbsolutePath(path, normalizedPath) &&
                       nodes_.contains(normalizedPath) &&
                       nodes_.at(normalizedPath).isDirectory;
  if (success) {
    currentDirectory_ = normalizedPath;
  }
  logOperation(FakeFileSystemOperationType::Chdir, path ? path : "", "", "",
               normalizedPath, success);
  return success;
}

bool FakeFileSystem::ListDirectory(const char *absoluteDir,
                                   etl::ivector<FileSystemEntry> *entries,
                                   const char *filter, bool directoriesOnly,
                                   bool includeHidden) {
  std::string normalizedDir;
  const bool hasValidDir = normalizeAbsolutePath(absoluteDir, normalizedDir) &&
                           nodes_.contains(normalizedDir) &&
                           nodes_.at(normalizedDir).isDirectory;
  const bool allowCall =
      !shouldFail(FakeFileSystemOperationType::ListDirectory) &&
      entries != nullptr && hasValidDir;

  if (entries != nullptr) {
    entries->clear();
  }

  bool success = allowCall;
  if (allowCall) {
    const std::string prefix =
        normalizedDir == "/" ? "/" : (normalizedDir + "/");

    for (const auto &[path, node] : nodes_) {
      if (path == normalizedDir || !startsWith(path, prefix)) {
        continue;
      }

      const std::string remainder = path.substr(prefix.size());
      if (remainder.empty() || remainder.find('/') != std::string::npos) {
        continue;
      }

      const bool isHidden =
          !remainder.empty() && static_cast<unsigned char>(remainder[0]) == '.';
      if (!includeHidden && isHidden) {
        continue;
      }
      if (directoriesOnly && !node.isDirectory) {
        continue;
      }
      if (!node.isDirectory && filter != nullptr && filter[0] != '\0' &&
          toLower(remainder).find(toLower(filter)) == std::string::npos) {
        continue;
      }
      if (entries->full()) {
        success = false;
        break;
      }

      FileSystemEntry entry;
      entry.name = remainder.c_str();
      entry.type = node.isDirectory ? PFT_DIR : PFT_FILE;
      entry.size = node.isDirectory ? 0 : node.contents.size();
      entry.isHidden = isHidden;
      entries->push_back(entry);
    }
  }

  logOperation(FakeFileSystemOperationType::ListDirectory,
               absoluteDir ? absoluteDir : "", filter ? filter : "", "",
               normalizedDir, success);
  return success;
}

bool FakeFileSystem::DeleteFile(const char *name) {
  std::string normalizedPath;
  const bool success = !shouldFail(FakeFileSystemOperationType::DeleteFile) &&
                       normalizeAbsolutePath(name, normalizedPath) &&
                       nodes_.contains(normalizedPath) &&
                       !nodes_.at(normalizedPath).isDirectory &&
                       nodes_.erase(normalizedPath) == 1;
  logOperation(FakeFileSystemOperationType::DeleteFile, name ? name : "", "",
               "", normalizedPath, success);
  return success;
}

bool FakeFileSystem::DeleteDir(const char *name) {
  std::string normalizedPath;
  bool success = !shouldFail(FakeFileSystemOperationType::DeleteDir) &&
                 normalizeAbsolutePath(name, normalizedPath) &&
                 normalizedPath != "/" && nodes_.contains(normalizedPath) &&
                 nodes_.at(normalizedPath).isDirectory;
  if (success) {
    for (const auto &[path, node] : nodes_) {
      (void)node;
      if (path != normalizedPath && startsWith(path, normalizedPath + "/")) {
        success = false;
        break;
      }
    }
  }
  if (success) {
    nodes_.erase(normalizedPath);
  }
  logOperation(FakeFileSystemOperationType::DeleteDir, name ? name : "", "", "",
               normalizedPath, success);
  return success;
}

bool FakeFileSystem::exists(const char *path) {
  std::string normalizedPath;
  const bool success = !shouldFail(FakeFileSystemOperationType::Exists) &&
                       normalizeAbsolutePath(path, normalizedPath) &&
                       nodes_.contains(normalizedPath);
  logOperation(FakeFileSystemOperationType::Exists, path ? path : "", "", "",
               normalizedPath, success);
  return success;
}

bool FakeFileSystem::makeDir(const char *path, bool pFlag) {
  std::string normalizedPath;
  const bool success = !shouldFail(FakeFileSystemOperationType::MakeDir) &&
                       normalizeAbsolutePath(path, normalizedPath) &&
                       createDirectory(normalizedPath, pFlag);
  logOperation(FakeFileSystemOperationType::MakeDir, path ? path : "", "", "",
               normalizedPath, success);
  return success;
}

bool FakeFileSystem::CopyFile(const char *srcFilename,
                              const char *destFilename) {
  std::string normalizedSrc;
  std::string normalizedDst;
  const bool success = !shouldFail(FakeFileSystemOperationType::CopyFile) &&
                       normalizeAbsolutePath(srcFilename, normalizedSrc) &&
                       normalizeAbsolutePath(destFilename, normalizedDst) &&
                       copyNode(normalizedSrc, normalizedDst);
  logOperation(FakeFileSystemOperationType::CopyFile,
               srcFilename ? srcFilename : "", destFilename ? destFilename : "",
               "", normalizedSrc + " -> " + normalizedDst, success);
  return success;
}

bool FakeFileSystem::MoveFile(const char *srcFilename,
                              const char *destFilename) {
  std::string normalizedSrc;
  std::string normalizedDst;
  const bool success = !shouldFail(FakeFileSystemOperationType::MoveFile) &&
                       normalizeAbsolutePath(srcFilename, normalizedSrc) &&
                       normalizeAbsolutePath(destFilename, normalizedDst) &&
                       moveNode(normalizedSrc, normalizedDst);
  logOperation(FakeFileSystemOperationType::MoveFile,
               srcFilename ? srcFilename : "", destFilename ? destFilename : "",
               "", normalizedSrc + " -> " + normalizedDst, success);
  return success;
}

void FakeFileSystem::AddDirectory(const char *absolutePath) {
  std::string normalizedPath;
  if (normalizeAbsolutePath(absolutePath, normalizedPath)) {
    createDirectory(normalizedPath, true);
  }
}

void FakeFileSystem::AddFile(const char *absolutePath, const char *contents) {
  std::string normalizedPath;
  if (normalizeAbsolutePath(absolutePath, normalizedPath)) {
    createFile(normalizedPath, contents ? contents : "");
  }
}

void FakeFileSystem::SetAllowLegacyCwdResolution(bool allow) {
  allowLegacyCwdResolution_ = allow;
}

void FakeFileSystem::FailNext(FakeFileSystemOperationType type) {
  failNextOperation_ = type;
}

void FakeFileSystem::ClearOperationLog() { operationLog_.clear(); }

const std::vector<FakeFileSystemOperation> &
FakeFileSystem::operationLog() const {
  return operationLog_;
}

const std::string &FakeFileSystem::currentDirectory() const {
  return currentDirectory_;
}

bool FakeFileSystem::normalizeAbsolutePath(const char *path,
                                           std::string &out) const {
  FileSystemPath::PathString normalizedPath;
  if (!FileSystemPath::NormalizePath(path, normalizedPath) ||
      !FileSystemPath::IsAbsolutePath(normalizedPath.c_str())) {
    out.clear();
    return false;
  }
  out = normalizedPath.c_str();
  return true;
}

bool FakeFileSystem::resolveRelativePath(const char *baseDir,
                                         const char *relativePath,
                                         std::string &out) const {
  FileSystemPath::PathString joinedPath;
  if (!FileSystemPath::JoinPath(baseDir, relativePath, joinedPath)) {
    out.clear();
    return false;
  }
  out = joinedPath.c_str();
  return true;
}

bool FakeFileSystem::parentDirectory(const std::string &path,
                                     std::string &out) const {
  FileSystemPath::PathString dirName;
  if (!FileSystemPath::DirName(path.c_str(), dirName)) {
    out.clear();
    return false;
  }
  out = dirName.c_str();
  return true;
}

bool FakeFileSystem::ensureDirectoryExists(const std::string &path) const {
  const auto it = nodes_.find(path);
  return it != nodes_.end() && it->second.isDirectory;
}

bool FakeFileSystem::createDirectory(const std::string &path,
                                     bool createParents) {
  if (path.empty()) {
    return false;
  }
  if (nodes_.contains(path)) {
    return nodes_.at(path).isDirectory;
  }

  std::string parent;
  if (!parentDirectory(path, parent)) {
    return false;
  }
  if (!ensureDirectoryExists(parent)) {
    if (!createParents || !createDirectory(parent, true)) {
      return false;
    }
  }

  nodes_[path] = Node{.isDirectory = true, .contents = ""};
  return true;
}

bool FakeFileSystem::createFile(const std::string &path,
                                const std::string &contents) {
  std::string parent;
  if (!parentDirectory(path, parent) || !createDirectory(parent, true)) {
    return false;
  }
  nodes_[path] = Node{.isDirectory = false, .contents = contents};
  return true;
}

bool FakeFileSystem::copyNode(const std::string &srcPath,
                              const std::string &destPath) {
  if (!nodes_.contains(srcPath)) {
    return false;
  }
  std::string parent;
  if (!parentDirectory(destPath, parent) || !ensureDirectoryExists(parent)) {
    return false;
  }

  const Node source = nodes_.at(srcPath);
  if (source.isDirectory) {
    if (!createDirectory(destPath, false)) {
      return false;
    }
    const std::string prefix = srcPath + "/";
    for (const auto &[path, node] : nodes_) {
      if (path == srcPath || !startsWith(path, prefix)) {
        continue;
      }
      const std::string suffix = path.substr(prefix.size());
      nodes_[destPath + "/" + suffix] = node;
    }
    return true;
  }

  nodes_[destPath] = source;
  return true;
}

bool FakeFileSystem::moveNode(const std::string &srcPath,
                              const std::string &destPath) {
  if (!copyNode(srcPath, destPath)) {
    return false;
  }

  std::vector<std::string> toErase;
  const std::string prefix = srcPath + "/";
  for (const auto &[path, node] : nodes_) {
    (void)node;
    if (path == srcPath || startsWith(path, prefix)) {
      toErase.push_back(path);
    }
  }
  for (const auto &path : toErase) {
    nodes_.erase(path);
  }
  return true;
}

void FakeFileSystem::logOperation(FakeFileSystemOperationType type,
                                  const std::string &path,
                                  const std::string &secondaryPath,
                                  const std::string &mode,
                                  const std::string &resolvedPath,
                                  bool success) {
  operationLog_.push_back(FakeFileSystemOperation{
      .type = type,
      .path = path,
      .secondaryPath = secondaryPath,
      .mode = mode,
      .resolvedPath = resolvedPath,
      .success = success,
  });
}

bool FakeFileSystem::shouldFail(FakeFileSystemOperationType type) {
  if (!failNextOperation_.has_value() || failNextOperation_.value() != type) {
    return false;
  }
  failNextOperation_.reset();
  return true;
}
