/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#ifndef _FAKE_FILESYSTEM_H_
#define _FAKE_FILESYSTEM_H_

#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

enum class FakeFileSystemOperationType {
  Open,
  Chdir,
  ListDirectory,
  DeleteFile,
  DeleteDir,
  Exists,
  MakeDir,
  CopyFile,
  MoveFile
};

struct FakeFileSystemOperation {
  FakeFileSystemOperationType type;
  std::string path;
  std::string secondaryPath;
  std::string mode;
  std::string resolvedPath;
  bool success;
};

class FakeFileSystem : public FileSystem {
public:
  FakeFileSystem();
  ~FakeFileSystem() override = default;

  FileHandle Open(const char *name, const char *mode) override;
  bool chdir(const char *path);
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

  void AddDirectory(const char *absolutePath);
  void AddFile(const char *absolutePath, const char *contents = "");
  void SetAllowLegacyCwdResolution(bool allow);
  void FailNext(FakeFileSystemOperationType type);
  void ClearOperationLog();

  const std::vector<FakeFileSystemOperation> &operationLog() const;
  const std::string &currentDirectory() const;

private:
  struct Node {
    bool isDirectory;
    std::string contents;
  };

  class FakeFile;

  bool normalizeAbsolutePath(const char *path, std::string &out) const;
  bool resolveRelativePath(const char *baseDir, const char *relativePath,
                           std::string &out) const;
  bool parentDirectory(const std::string &path, std::string &out) const;
  bool ensureDirectoryExists(const std::string &path) const;
  bool createDirectory(const std::string &path, bool createParents);
  bool createFile(const std::string &path, const std::string &contents);
  bool copyNode(const std::string &srcPath, const std::string &destPath);
  bool moveNode(const std::string &srcPath, const std::string &destPath);
  void logOperation(FakeFileSystemOperationType type, const std::string &path,
                    const std::string &secondaryPath, const std::string &mode,
                    const std::string &resolvedPath, bool success);
  bool shouldFail(FakeFileSystemOperationType type);

  std::map<std::string, Node> nodes_;
  std::vector<FakeFileSystemOperation> operationLog_;
  std::string currentDirectory_;
  bool allowLegacyCwdResolution_;
  std::optional<FakeFileSystemOperationType> failNextOperation_;
};

#endif
