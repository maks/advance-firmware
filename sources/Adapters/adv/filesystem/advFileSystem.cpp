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

#include "advFileSystem.h"
#include "Externals/etl/include/etl/pool.h"
#include "Externals/etl/include/etl/string_utilities.h"
#include "FileHandle.h"
#include "FreeRTOS.h"
#include "System/FileSystem/PathUtils.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include "task.h"

FATFS SDFatFS;
char SDPath[4];

static etl::pool<PI_File, FF_FS_LOCK> filePool;

namespace {

static constexpr uint32_t FileCopyBufferSize = 64U * 1024U;

static BYTE fileCopyBuffer[FileCopyBufferSize]
    __attribute__((section(".SDRAM1_NC"))) __attribute__((aligned(32)));

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

} // namespace

advFileSystem::advFileSystem() {

  // Link FatFs driver
  FATFS_LinkDriver(&SD_DMA_Driver, SDPath);

  // Check for the common case, FAT filesystem as first partition
  Trace::Log("FILESYSTEM", "Try to mount SD Card");
  if (f_mount(&SDFatFS, (TCHAR const *)SDPath, 0) == FR_OK) {
    Trace::Log("FILESYSTEM", "Mounted SD Card FAT Filesystem first partition");
    return;
  }
  // Do we have any kind of card?
  // TODO: Check SD_DET pin
  //  if (!sd.card() || sd.sdErrorCode() != 0) {
  //    Trace::Log("FILESYSTEM", "No SD Card present");
  //    return;
  //  }
}

FileHandle advFileSystem::Open(const char *name, const char *mode) {
  const bool hasPlus = (mode != nullptr) && (std::strchr(mode, '+') != nullptr);
  BYTE rmode = 0;

  if (!mode || !*mode) {
    Trace::Error("Invalid mode: %s", mode ? mode : "(null)");
    return FileHandle();
  }

  Trace::Log("FILESYSTEM", "Open file:%s, mode:%s", name, mode);

  switch (*mode) {
  case 'r':
    rmode = FA_READ | (hasPlus ? FA_WRITE : 0);
    break;
  case 'w':
    rmode = FA_CREATE_ALWAYS | FA_WRITE | (hasPlus ? FA_READ : 0);
    break;
  default:
    Trace::Error("Invalid mode: %s", mode);
    return FileHandle();
  }
  FIL cwd;
  PI_File *wFile = 0;
  FRESULT res = f_open(&cwd, name, rmode);
  if (res == FR_LOCKED) {
    Trace::Error("FILESYSTEM: Cannot open file (locked): %s (mode:%s)", name,
                 mode);
    return FileHandle();
  }
  if (res != FR_OK) {
    Trace::Error("FILESYSTEM: Cannot open file:%s (mode:%s, err:%d)", name,
                 mode, static_cast<int>(res));
    return FileHandle();
  }
  taskENTER_CRITICAL();
  wFile = filePool.create(cwd);
  taskEXIT_CRITICAL();
  if (wFile == nullptr) {
    // Avoid leaking an open FATFS object when the pool is exhausted.
    f_close(&cwd);
    Trace::Error("FILESYSTEM: No file slots available (max %d)",
                 static_cast<int>(FF_FS_LOCK));
    return FileHandle();
  }
  return MakeFileHandle(wFile);
}

bool advFileSystem::ListDirectory(const char *absoluteDir,
                                  etl::ivector<FileSystemEntry> *entries,
                                  const char *filter, bool directoriesOnly,
                                  bool includeHidden) {
  SetLastListDirectoryStatus(DirectoryListStatus::Ok);

  if (entries == nullptr) {
    Trace::Error("FILESYSTEM: ListDirectory received null entries vector");
    SetLastListDirectoryStatus(DirectoryListStatus::Error);
    return false;
  }

  entries->clear();
  FileSystemPath::PathString normalizedFilter;
  if (filter != nullptr) {
    normalizedFilter = filter;
  }

  FileSystemPath::PathString normalizedDir;
  if (!FileSystemPath::NormalizePath(absoluteDir, normalizedDir) ||
      !FileSystemPath::IsAbsolutePath(normalizedDir.c_str())) {
    Trace::Error("FILESYSTEM: ListDirectory requires an absolute directory");
    SetLastListDirectoryStatus(DirectoryListStatus::Error);
    return false;
  }

  DIR dir;
  FRESULT res = f_opendir(&dir, normalizedDir.c_str());
  if (res != FR_OK) {
    Trace::Error("FILESYSTEM: Failed to open directory %s (err:%d)",
                 normalizedDir.c_str(), static_cast<int>(res));
    SetLastListDirectoryStatus(DirectoryListStatus::Error);
    return false;
  }

  bool success = true;
  for (;;) {
    FILINFO fno;
    res = f_readdir(&dir, &fno);
    if (res != FR_OK) {
      Trace::Error("FILESYSTEM: Failed to read directory %s (err:%d)",
                   normalizedDir.c_str(), static_cast<int>(res));
      success = false;
      SetLastListDirectoryStatus(DirectoryListStatus::Error);
      break;
    }
    if (fno.fname[0] == '\0') {
      break;
    }
    if (std::strcmp(fno.fname, ".") == 0 || std::strcmp(fno.fname, "..") == 0) {
      continue;
    }

    const bool isDir = (fno.fattrib & AM_DIR) != 0;
    const bool isHidden = fno.fname[0] == '.';
    if (!includeHidden && isHidden) {
      continue;
    }
    if (directoriesOnly && !isDir) {
      continue;
    }
    FileSystemPath::PathString entryName(fno.fname);
    if (entryName.is_truncated()) {
      Trace::Error("FILESYSTEM: Entry name truncated in %s",
                   normalizedDir.c_str());
      success = false;
      SetLastListDirectoryStatus(DirectoryListStatus::Error);
      break;
    }
    if (!isDir && !matchesFilter(entryName, normalizedFilter)) {
      continue;
    }
    if (entries->full()) {
      Trace::Error("FILESYSTEM: ListDirectory output buffer is full");
      success = false;
      SetLastListDirectoryStatus(DirectoryListStatus::Truncated);
      break;
    }

    FileSystemEntry entry;
    entry.name = entryName;
    entry.type = isDir ? PFT_DIR : PFT_FILE;
    entry.size = isDir ? 0 : fno.fsize;
    entry.isHidden = isHidden;
    entries->push_back(entry);
  }

  f_closedir(&dir);
  return success;
}

bool advFileSystem::DeleteFile(const char *path) {
  FILINFO fil;
  FRESULT res;
  res = f_stat(path, &fil);
  if (res != FR_OK) {
    Trace::Error("Path does not exist");
    return false;
  }
  if (fil.fattrib & AM_DIR) {
    Trace::Error("Path is not a file");
    return false;
  }
  return f_unlink(path) == FR_OK;
}

// directory has to be empty
bool advFileSystem::DeleteDir(const char *path) {
  FILINFO fil;
  FRESULT res;
  res = f_stat(path, &fil);
  if (res != FR_OK) {
    Trace::Error("Path does not exist");
    return false;
  }
  if (!(fil.fattrib & AM_DIR)) {
    Trace::Error("Path is not a directory");
    return false;
  }
  return f_unlink(path) == FR_OK;
}

void advFileSystem::BeginBatch() {}

void advFileSystem::EndBatch() {}

bool advFileSystem::exists(const char *path) {
  if (path != nullptr && std::strcmp(path, "/") == 0) {
    DIR dir;
    const FRESULT dirResult = f_opendir(&dir, path);
    if (dirResult == FR_OK) {
      f_closedir(&dir);
      return true;
    }
    return false;
  }

  FILINFO fno;
  FRESULT res = f_stat(path, &fno);
  return res == FR_OK;
}

/**
 * Create a directory at the specified path.
 *
 * \param[in] path The path where the directory will be created.
 * \param[in] pFlag If true, create missing parent directories.
 *
 * \return true if the directory was successfully created, false otherwise.
 */
bool advFileSystem::makeDir(const char *path, bool pFlag) {
  if (!pFlag) {
    return f_mkdir(path) == FR_OK;
  }

  char tempPath[256];
  strncpy(tempPath, path, sizeof(tempPath));
  tempPath[sizeof(tempPath) - 1] = '\0'; // ensure null-termination

  size_t len = strlen(tempPath);
  if (len == 0)
    return false;

  // Iterate through the path and create components
  for (size_t i = 1; i < len; ++i) {
    if (tempPath[i] == '/') {
      tempPath[i] = '\0';

      FRESULT res = f_mkdir(tempPath);
      if (res != FR_OK && res != FR_EXIST) {
        return false;
      }

      tempPath[i] = '/'; // restore slash
    }
  }

  // Make final directory
  FRESULT res = f_mkdir(tempPath);
  return res == FR_OK || res == FR_EXIST;
}

bool advFileSystem::CopyFile(const char *srcFilename,
                             const char *destFilename) {
  FIL fsrc, fdst; // File objects
  UINT br, bw;    // File read/write count
  FRESULT res;
  // Open source file on the drive 1
  res = f_open(&fsrc, srcFilename, FA_READ);
  if (res != FR_OK)
    return false;

  // Create destination file on the drive 0
  res = f_open(&fdst, destFilename, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK) {
    f_close(&fsrc);
    return false;
  }

  // Copy source to destination
  for (;;) {
    res = f_read(&fsrc, fileCopyBuffer, sizeof(fileCopyBuffer),
                 &br); // Read a chunk of data from the source file
    if (br == 0)
      break; // error or eof
    res = f_write(&fdst, fileCopyBuffer, br,
                  &bw); // Write it to the destination file
    if (bw < br)
      break; // error or disk full
  }

  // Close open files
  f_close(&fsrc);
  f_close(&fdst);

  return res == FR_OK;
}

bool advFileSystem::MoveFile(const char *srcFilename,
                             const char *destFilename) {
  return f_rename(srcFilename, destFilename) == FR_OK;
}

PI_File::PI_File(FIL file) : file_(file), isOpen_(true) {}

PI_File::~PI_File() { Close(); }

/**
 * Read data from a file starting at the current position.
 *
 * \param[out] buf Pointer to the location that will receive the data.
 *
 * \param[in] size Maximum number of bytes to read.
 *
 * \return For success read() returns the number of bytes read.
 * A value less than \a count, including zero, will be returned
 * if end of file is reached.
 * If an error occurs, read() returns -1.  Possible errors include
 * read() called before a file has been opened, corrupt file system
 * or an I/O error occurred.
 */
int PI_File::Read(void *ptr, int size) {
  UINT read;
  FRESULT res = f_read(&file_, ptr, size, &read);
  if (res != FR_OK) {
    return -1;
  }
  return read;
}

void PI_File::Seek(long offset, int whence) {
  FRESULT res;
  UNUSED(res);
  switch (whence) {
  case SEEK_SET:
    res = f_lseek(&file_, offset);
    break;
  case SEEK_CUR:
    res = f_lseek(&file_, f_tell(&file_) + offset);
    break;
  case SEEK_END:
    res = f_lseek(&file_, f_size(&file_));
    break;
  default:
    Trace::Error("Invalid seek whence: %s", whence);
  }
}

int PI_File::GetC() {
  BYTE c = 0;
  UINT bytesRead = 0;
  FRESULT res = f_read(&file_, &c, 1, &bytesRead);
  if ((res != FR_OK) || (bytesRead == 0)) {
    return EOF;
  }
  return static_cast<int>(c);
}

int PI_File::Write(const void *ptr, int size, int nmemb) {
  UINT written;
  FRESULT res = f_write(&file_, ptr, size * nmemb, &written);
  if (res != FR_OK) {
    Trace::Error("FILESYSTEM: f_write failed err:%d", static_cast<int>(res));
    return -1;
  }
  return written;
}

long PI_File::Tell() { return f_tell(&file_); }

int PI_File::Error() { return f_error(&file_); }

bool PI_File::Close() {
  if (!isOpen_) {
    return true;
  }

  FRESULT res = f_close(&file_);
  if (!res) {
    isOpen_ = false;
  } else {
    Trace::Error("FILESYSTEM: f_close failed err:%d", static_cast<int>(res));
  }
  return res == FR_OK;
}

bool PI_File::Sync() {
  FRESULT res = f_sync(&file_);
  if (res != FR_OK) {
    Trace::Error("FILESYSTEM: f_sync failed err:%d", static_cast<int>(res));
  }
  return res == FR_OK;
}

void PI_File::Dispose() {
  Close();
  taskENTER_CRITICAL();
  filePool.destroy(this);
  taskEXIT_CRITICAL();
}
