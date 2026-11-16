/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */
#ifndef _PICO_FILESYSTEM_H_
#define _PICO_FILESYSTEM_H_

#include "Externals/FatFs/source/ff.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/T_Factory.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include "ff_gen_drv.h"
#include "sd_diskio.h"

extern FATFS SDFatFS;
extern char SDPath[4]; /* SD logical drive path */

class PI_File : public I_File {
public:
  PI_File(FIL file);
  ~PI_File();
  int Read(void *ptr, int size) override;
  int GetC() override;
  int Write(const void *ptr, int size, int nmemb) override;
  void Seek(long offset, int whence) override;
  long Tell() override;
  bool Seek64(uint64_t offset, int whence) override;
  uint64_t Tell64() override;
  bool Close() override;
  int Error() override;
  bool Sync() override;
  void Dispose() override;

private:
  FIL file_;
  // isOpen_ needed as a safeguard because we still use the legacy explicit
  // close from TinyXML
  bool isOpen_;
};

class advFileSystem : public FileSystem {
public:
  advFileSystem(); // OK
  virtual ~advFileSystem(){};
  virtual FileHandle Open(const char *name, const char *mode) override;
  virtual bool ListDirectory(const char *absoluteDir,
                             etl::ivector<FileSystemEntry> *entries,
                             const char *filter, bool directoriesOnly,
                             bool includeHidden = false) override;
  bool ListDirectoryPage(const char *absoluteDir,
                         etl::ivector<FileSystemEntry> *entries,
                         const char *filter, bool directoriesOnly,
                         bool includeHidden, size_t offset,
                         size_t &totalMatches) override;
  virtual bool DeleteFile(const char *name) override;
  virtual bool DeleteDir(const char *name) override;
  virtual void BeginBatch() override;
  virtual void EndBatch() override;
  virtual bool exists(const char *path) override;
  virtual bool makeDir(const char *path, bool pFlag = false) override;
  virtual bool CopyFile(const char *srcFilename,
                        const char *destFilename) override;
  virtual bool MoveFile(const char *srcFilename,
                        const char *destFilename) override;
  bool GetFreeSpaceBytes(const char *path, uint64_t &bytes) override;
  bool RenameDir(const char *srcPath, const char *destPath) override;
  virtual bool isExFat() { return false; }; // not used on the Advance
};

#endif
