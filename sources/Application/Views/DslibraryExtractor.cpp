/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "DslibraryExtractor.h"

#include "Externals/miniz/miniz.h"
#include "System/FileSystem/FileHandle.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <strings.h>
#if defined(ADV)
#include "FreeRTOS.h"
#include "task.h"
#endif

namespace {
#if defined(HOST_TEST)
#define DSLIBRARY_SDRAM
#define DSLIBRARY_EXTERNAL
#else
#define DSLIBRARY_SDRAM                                                        \
  __attribute__((section(".SDRAM1_NC"))) __attribute__((aligned(32)))
#define DSLIBRARY_EXTERNAL                                                     \
  __attribute__((section(".RAM_EXT"))) __attribute__((aligned(32)))
#endif
constexpr uint32_t kMaxEntries = 4096;
constexpr uint64_t kMaxOutputBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMinimumFreeBytes = 256ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxDepth = 16;
constexpr char kIncompleteMarker[] = ".dslibrary-incomplete";
constexpr char kMarkerContents[] = "picoTracker dslibrary staging v1";
std::atomic<bool> extractionActive{false};
etl::vector<FileSystemEntry, MAX_FILE_INDEX_SIZE> cleanupEntries
    DSLIBRARY_EXTERNAL;
char cleanupPathStack[kMaxDepth + 1][PFILENAME_SIZE] DSLIBRARY_EXTERNAL;
uint8_t archiveWorkspace[896U * 1024U] DSLIBRARY_EXTERNAL;
size_t archiveWorkspaceUsed = 0;
struct alignas(32) ArchiveAllocationHeader {
  size_t size;
  uint8_t reserved[24];
};
std::atomic<bool> extractionCancel{false};
std::atomic<int> asyncState{0};
uint64_t asyncExtracted = 0;
uint64_t asyncTotal = 0;
char asyncArchivePath[PFILENAME_SIZE] DSLIBRARY_EXTERNAL;
char asyncDestinationPath[PFILENAME_SIZE] DSLIBRARY_EXTERNAL;
char asyncFilename[PFILENAME_SIZE] DSLIBRARY_EXTERNAL;
char asyncStage[24] DSLIBRARY_EXTERNAL;
char firstPresetPath[PFILENAME_SIZE]{};
uint32_t presetCount = 0;
#if defined(ADV)
StaticTask_t extractionTaskStorage{};
StackType_t extractionTaskStack[6144] DSLIBRARY_EXTERNAL;
TaskHandle_t extractionTaskHandle = nullptr;
#endif

void *ArchiveAlloc(void *, size_t items, size_t size) {
  if (size != 0 && items > SIZE_MAX / size) {
    return nullptr;
  }
  const size_t requested = items * size;
  if (requested > SIZE_MAX - sizeof(ArchiveAllocationHeader) - 31U) {
    return nullptr;
  }
  const size_t extent =
      (requested + sizeof(ArchiveAllocationHeader) + 31U) & ~31U;
  if (extent < requested ||
      extent > sizeof(archiveWorkspace) - archiveWorkspaceUsed) {
    return nullptr;
  }
  auto *header = reinterpret_cast<ArchiveAllocationHeader *>(
      archiveWorkspace + archiveWorkspaceUsed);
  header->size = requested;
  void *allocation = header + 1;
  archiveWorkspaceUsed += extent;
  return allocation;
}

void *ArchiveRealloc(void *opaque, void *address, size_t items, size_t size) {
  if (address == nullptr) {
    return ArchiveAlloc(opaque, items, size);
  }
  if (size != 0 && items > SIZE_MAX / size) {
    return nullptr;
  }
  const size_t requested = items * size;
  auto *header = static_cast<ArchiveAllocationHeader *>(address) - 1;
  const size_t oldSize = header->size;
  void *replacement = ArchiveAlloc(opaque, 1, requested);
  if (replacement == nullptr) {
    return nullptr;
  }
  std::memcpy(replacement, address, oldSize < requested ? oldSize : requested);
  return replacement;
}

void ArchiveFree(void *, void *) {}

struct ArchiveIO {
  FileHandle file;
  bool failed = false;
};

struct OutputIO {
  FileHandle file;
  bool failed = false;
  bool cancelled = false;
  DslibraryExtractor::ProgressCallback callback = nullptr;
  void *context = nullptr;
  const char *filename = nullptr;
  uint64_t *extracted = nullptr;
  uint64_t total = 0;
  uint64_t currentWritten = 0;
};

size_t ReadArchive(void *opaque, mz_uint64 offset, void *buffer, size_t size) {
  auto &io = *static_cast<ArchiveIO *>(opaque);
  size_t totalRead = 0;
  while (totalRead < size) {
    if (extractionCancel.load(std::memory_order_acquire) ||
        !io.file->Seek64(offset + totalRead, SEEK_SET)) {
      io.failed = true;
      return totalRead;
    }
    const size_t chunk =
        size - totalRead < 16U * 1024U ? size - totalRead : 16U * 1024U;
    const int read = io.file->Read(static_cast<uint8_t *>(buffer) + totalRead,
                                   static_cast<int>(chunk));
    if (read < 0) {
      io.failed = true;
      return totalRead;
    }
    if (read == 0) {
      break;
    }
    totalRead += static_cast<size_t>(read);
  }
  return totalRead;
}

size_t WriteOutput(void *opaque, mz_uint64, const void *buffer, size_t size) {
  auto &io = *static_cast<OutputIO *>(opaque);
  if (extractionCancel.load(std::memory_order_acquire)) {
    io.cancelled = true;
    io.failed = true;
    return 0;
  }
  if (io.callback != nullptr &&
      !io.callback({"Extracting", io.filename,
                    *io.extracted + io.currentWritten, io.total},
                   io.context)) {
    io.cancelled = true;
    io.failed = true;
    return 0;
  }
  const int written = io.file->Write(buffer, static_cast<int>(size), 1);
  if (written != static_cast<int>(size)) {
    io.failed = true;
    return 0;
  }
  io.currentWritten += size;
  return size;
}

bool IsPathSafe(const char *name, bool directory) {
  if (name == nullptr || name[0] == '\0' || name[0] == '/' || name[0] == '\\' ||
      std::strchr(name, ':') != nullptr) {
    return false;
  }
  size_t depth = 0;
  const char *part = name;
  for (const char *cursor = name;; ++cursor) {
    const char ch = *cursor;
    if (ch != '\0' && (static_cast<unsigned char>(ch) < 32 ||
                       std::strchr("<>:\"|?*", ch) != nullptr)) {
      return false;
    }
    if (ch == '\\') {
      return false;
    }
    if (ch == '/' || ch == '\0') {
      const size_t length = static_cast<size_t>(cursor - part);
      if (length == 0) {
        if (ch == '\0' && directory && cursor != name && cursor[-1] == '/') {
          break;
        }
        return false;
      }
      if ((length == 1 && part[0] == '.') ||
          (length == 2 && part[0] == '.' && part[1] == '.')) {
        return false;
      }
      if (length > 0 && (part[length - 1] == '.' || part[length - 1] == ' ')) {
        return false;
      }
      if (++depth > kMaxDepth) {
        return false;
      }
      if (ch == '\0') {
        break;
      }
      part = cursor + 1;
    }
  }
  return std::strlen(name) < PFILENAME_SIZE;
}

bool IsReservedMarkerPath(const char *name) {
  constexpr size_t markerLength = sizeof(kIncompleteMarker) - 1;
  return strncasecmp(name, kIncompleteMarker, markerLength) == 0 &&
         (name[markerLength] == '\0' || name[markerLength] == '/');
}

bool HasCaseFoldedComponentCollision(const char *left, const char *right) {
  const char *leftPart = left;
  const char *rightPart = right;
  while (*leftPart != '\0' && *rightPart != '\0') {
    const char *leftEnd = std::strchr(leftPart, '/');
    const char *rightEnd = std::strchr(rightPart, '/');
    if (leftEnd == nullptr) {
      leftEnd = leftPart + std::strlen(leftPart);
    }
    if (rightEnd == nullptr) {
      rightEnd = rightPart + std::strlen(rightPart);
    }
    const size_t leftLength = static_cast<size_t>(leftEnd - leftPart);
    const size_t rightLength = static_cast<size_t>(rightEnd - rightPart);
    if (leftLength != rightLength ||
        strncasecmp(leftPart, rightPart, leftLength) != 0) {
      return false;
    }
    if (std::memcmp(leftPart, rightPart, leftLength) != 0) {
      return true;
    }
    leftPart = *leftEnd == '/' ? leftEnd + 1 : leftEnd;
    rightPart = *rightEnd == '/' ? rightEnd + 1 : rightEnd;
  }
  return false;
}

bool Join(const char *root, const char *relative,
          FileSystemPath::PathString &output) {
  return FileSystemPath::JoinPath(root, relative, output) &&
         !output.is_truncated();
}

bool EnsureParentDirs(FileSystem *fs, const char *root, const char *relative) {
  FileSystemPath::PathString path;
  if (!Join(root, relative, path)) {
    return false;
  }
  const char *lastSlash = std::strrchr(path.c_str(), '/');
  if (lastSlash == nullptr || lastSlash == path.c_str()) {
    return true;
  }
  FileSystemPath::PathString parent;
  parent.assign(path.c_str(), static_cast<size_t>(lastSlash - path.c_str()));
  return fs->makeDir(parent.c_str(), true);
}

bool DeleteTree(FileSystem *fs, const char *path) {
  size_t depth = 0;
  std::strncpy(cleanupPathStack[0], path, PFILENAME_SIZE - 1);
  cleanupPathStack[0][PFILENAME_SIZE - 1] = '\0';
  while (true) {
    const char *current = cleanupPathStack[depth];
    cleanupEntries.clear();
    size_t totalEntries = 0;
    if (!fs->ListDirectoryPage(current, &cleanupEntries, nullptr, false, true,
                               0, totalEntries)) {
      return false;
    }
    if (!cleanupEntries.empty()) {
      const auto &entry = cleanupEntries[0];
      FileSystemPath::PathString child;
      if (!Join(current, entry.name.c_str(), child)) {
        return false;
      }
      if (entry.type != PFT_DIR) {
        if (!fs->DeleteFile(child.c_str())) {
          return false;
        }
      } else {
        if (depth >= kMaxDepth || child.size() >= PFILENAME_SIZE) {
          return false;
        }
        ++depth;
        std::strncpy(cleanupPathStack[depth], child.c_str(),
                     PFILENAME_SIZE - 1);
        cleanupPathStack[depth][PFILENAME_SIZE - 1] = '\0';
      }
      continue;
    }
    if (depth == 0) {
      return fs->DeleteDir(current);
    }
    if (!fs->DeleteDir(current)) {
      return false;
    }
    --depth;
  }
}

bool WriteOwnershipMarker(FileSystem *fs, const char *path) {
  auto marker = fs->Open(path, "w");
  if (!marker) {
    return false;
  }
  return marker->Write(kMarkerContents, 1,
                       static_cast<int>(sizeof(kMarkerContents) - 1)) ==
             static_cast<int>(sizeof(kMarkerContents) - 1) &&
         marker->Sync();
}

bool AsyncProgress(const DslibraryExtractor::Progress &progress, void *) {
#if defined(ADV)
  taskENTER_CRITICAL();
#endif
  std::strncpy(asyncStage, progress.stage ? progress.stage : "",
               sizeof(asyncStage) - 1);
  asyncStage[sizeof(asyncStage) - 1] = '\0';
  std::strncpy(asyncFilename, progress.filename ? progress.filename : "",
               sizeof(asyncFilename) - 1);
  asyncFilename[sizeof(asyncFilename) - 1] = '\0';
  asyncExtracted = progress.extractedBytes;
  asyncTotal = progress.totalBytes;
#if defined(ADV)
  taskEXIT_CRITICAL();
#endif
  return !extractionCancel.load(std::memory_order_acquire);
}

DslibraryExtractor::Result
ExtractClaimed(const char *archivePath, const char *destinationPath,
               DslibraryExtractor::ProgressCallback callback, void *context);

#if defined(ADV)
void ExtractionTask(void *) {
  const auto result = ExtractClaimed(asyncArchivePath, asyncDestinationPath,
                                     AsyncProgress, nullptr);
  asyncState.store(static_cast<int>(result) + 1, std::memory_order_release);
  extractionTaskHandle = nullptr;
  vTaskDelete(nullptr);
}
#endif

} // namespace

bool DslibraryExtractor::IsActive() {
  return extractionActive.load(std::memory_order_acquire);
}

bool DslibraryExtractor::StartAsync(const char *archivePath,
                                    const char *destinationPath) {
#if defined(ADV)
  if (archivePath == nullptr || destinationPath == nullptr ||
      std::strlen(archivePath) >= sizeof(asyncArchivePath) ||
      std::strlen(destinationPath) >= sizeof(asyncDestinationPath) ||
      asyncState.load(std::memory_order_acquire) != 0) {
    return false;
  }
  bool expected = false;
  if (!extractionActive.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
    return false;
  }
  std::strcpy(asyncArchivePath, archivePath);
  std::strcpy(asyncDestinationPath, destinationPath);
  asyncStage[0] = '\0';
  asyncFilename[0] = '\0';
  extractionCancel.store(false, std::memory_order_release);
  asyncExtracted = 0;
  asyncTotal = 0;
  asyncState.store(-1, std::memory_order_release);
  extractionTaskHandle = xTaskCreateStatic(
      ExtractionTask, "Dslibrary", 6144, nullptr, tskIDLE_PRIORITY + 1,
      extractionTaskStack, &extractionTaskStorage);
  if (extractionTaskHandle == nullptr) {
    extractionActive.store(false, std::memory_order_release);
    asyncState.store(0, std::memory_order_release);
    return false;
  }
  return true;
#else
  (void)archivePath;
  (void)destinationPath;
  return false;
#endif
}

void DslibraryExtractor::Cancel() {
  extractionCancel.store(true, std::memory_order_release);
}

bool DslibraryExtractor::GetAsyncProgress(Progress &progress, char *stage,
                                          size_t stageCapacity, char *filename,
                                          size_t filenameCapacity) {
  if (asyncState.load(std::memory_order_acquire) == 0) {
    return false;
  }
#if defined(ADV)
  taskENTER_CRITICAL();
#endif
  if (stage != nullptr && stageCapacity > 0) {
    std::strncpy(stage, asyncStage, stageCapacity - 1);
    stage[stageCapacity - 1] = '\0';
    progress.stage = stage;
  } else {
    progress.stage = "";
  }
  if (filename != nullptr && filenameCapacity > 0) {
    std::strncpy(filename, asyncFilename, filenameCapacity - 1);
    filename[filenameCapacity - 1] = '\0';
    progress.filename = filename;
  } else {
    progress.filename = "";
  }
  progress.extractedBytes = asyncExtracted;
  progress.totalBytes = asyncTotal;
#if defined(ADV)
  taskEXIT_CRITICAL();
#endif
  return true;
}

bool DslibraryExtractor::TakeAsyncResult(Result &result) {
  int state = asyncState.load(std::memory_order_acquire);
  if (state <= 0) {
    return false;
  }
  if (!asyncState.compare_exchange_strong(state, 0,
                                          std::memory_order_acq_rel)) {
    return false;
  }
  result = static_cast<Result>(state - 1);
  return true;
}

uint32_t DslibraryExtractor::GetPresetSummary(char *firstPath,
                                              size_t pathCapacity) {
  if (firstPath != nullptr && pathCapacity > 0) {
    std::strncpy(firstPath, firstPresetPath, pathCapacity - 1);
    firstPath[pathCapacity - 1] = '\0';
  }
  return presetCount;
}

bool DslibraryExtractor::CleanupIncompleteStage(const char *stagingPath) {
  auto *fs = FileSystem::GetInstance();
  FileSystemPath::PathString marker;
  if (fs == nullptr || stagingPath == nullptr ||
      !Join(stagingPath, kIncompleteMarker, marker)) {
    return false;
  }
  auto markerFile = fs->Open(marker.c_str(), "r");
  if (!markerFile) {
    return false;
  }
  char markerContents[sizeof(kMarkerContents) - 1]{};
  const int markerRead =
      markerFile->Read(markerContents, sizeof(markerContents));
  if (markerRead != static_cast<int>(sizeof(markerContents)) ||
      std::memcmp(markerContents, kMarkerContents, sizeof(markerContents)) !=
          0) {
    return false;
  }
  markerFile.reset();
  if (!fs->DeleteFile(marker.c_str())) {
    return false;
  }
  if (DeleteTree(fs, stagingPath)) {
    return true;
  }
  WriteOwnershipMarker(fs, marker.c_str());
  return false;
}

DslibraryExtractor::Result
DslibraryExtractor::Extract(const char *archivePath,
                            const char *destinationPath,
                            ProgressCallback callback, void *context) {
  auto *fs = FileSystem::GetInstance();
  if (fs == nullptr || archivePath == nullptr || destinationPath == nullptr) {
    return Result::FilesystemError;
  }
  bool expected = false;
  if (!extractionActive.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
    return Result::FilesystemError;
  }
  extractionCancel.store(false, std::memory_order_release);
  return ExtractClaimed(archivePath, destinationPath, callback, context);
}

namespace {
DslibraryExtractor::Result
ExtractClaimed(const char *archivePath, const char *destinationPath,
               DslibraryExtractor::ProgressCallback callback, void *context) {
  using Result = DslibraryExtractor::Result;
  auto *fs = FileSystem::GetInstance();
  if (fs == nullptr || archivePath == nullptr || destinationPath == nullptr) {
    extractionActive.store(false, std::memory_order_release);
    return DslibraryExtractor::Result::FilesystemError;
  }
  struct ActiveGuard {
    ~ActiveGuard() { extractionActive.store(false, std::memory_order_release); }
  } activeGuard;
  if (fs->exists(destinationPath)) {
    return Result::ExistingDestination;
  }

  auto archiveFile = fs->Open(archivePath, "r");
  if (!archiveFile) {
    return Result::FilesystemError;
  }
  if (!archiveFile->Seek64(0, SEEK_END)) {
    return Result::FilesystemError;
  }
  const uint64_t archiveSize = archiveFile->Tell64();
  if (archiveSize == 0 || archiveSize == UINT64_MAX ||
      !archiveFile->Seek64(0, SEEK_SET)) {
    return Result::InvalidArchive;
  }
  if (archiveSize > UINT32_MAX) {
    return Result::UnsupportedArchive;
  }

  ArchiveIO input{std::move(archiveFile)};
  archiveWorkspaceUsed = 0;
  mz_zip_archive archive;
  mz_zip_zero_struct(&archive);
  archive.m_pAlloc = ArchiveAlloc;
  archive.m_pRealloc = ArchiveRealloc;
  archive.m_pFree = ArchiveFree;
  archive.m_pRead = ReadArchive;
  archive.m_pIO_opaque = &input;
  if (!mz_zip_reader_init(&archive, archiveSize, 0)) {
    const auto error = mz_zip_get_last_error(&archive);
    if (extractionCancel.load(std::memory_order_acquire)) {
      return Result::Cancelled;
    }
    if (error == MZ_ZIP_ALLOC_FAILED) {
      return Result::OutOfMemory;
    }
    return error == MZ_ZIP_UNSUPPORTED_MULTIDISK ||
                   error == MZ_ZIP_UNSUPPORTED_FEATURE
               ? Result::UnsupportedArchive
               : Result::InvalidArchive;
  }
  const mz_uint count = mz_zip_reader_get_num_files(&archive);
  Result result = Result::Success;
  uint64_t outputTotal = 0;
  presetCount = 0;
  firstPresetPath[0] = '\0';
  if (mz_zip_is_zip64(&archive) || count == 0 || count > kMaxEntries) {
    result = Result::UnsupportedArchive;
  }

  // Validate all central directory records before creating any output.
  for (mz_uint index = 0; result == Result::Success && index < count; ++index) {
    char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE]{};
    mz_zip_archive_file_stat stat{};
    if (callback != nullptr && !callback({"Inspecting", "", 0, 0}, context)) {
      result = Result::Cancelled;
      break;
    }
    const bool directory = mz_zip_reader_is_file_a_directory(&archive, index);
    if (mz_zip_reader_get_filename(&archive, index, name, sizeof(name)) == 0 ||
        !IsPathSafe(name, directory) || IsReservedMarkerPath(name) ||
        !mz_zip_reader_file_stat(&archive, index, &stat)) {
      result = Result::InvalidPath;
      break;
    }
    if (!mz_zip_reader_is_file_supported(&archive, index) ||
        stat.m_is_encrypted || (stat.m_method != 0 && stat.m_method != 8)) {
      result = Result::UnsupportedArchive;
      break;
    }
    if (!directory) {
      if (stat.m_uncomp_size > 0xFFFFFFFFULL ||
          outputTotal > kMaxOutputBytes - stat.m_uncomp_size) {
        result = Result::UnsupportedArchive;
        break;
      }
      outputTotal += stat.m_uncomp_size;
    }
    const size_t nameLength = std::strlen(name);
    constexpr char presetExtension[] = ".dspreset";
    constexpr size_t presetExtensionLength = sizeof(presetExtension) - 1;
    if (!directory && nameLength > presetExtensionLength &&
        strcasecmp(name + nameLength - presetExtensionLength,
                   presetExtension) == 0) {
      ++presetCount;
      if (presetCount == 1) {
        std::strncpy(firstPresetPath, name, sizeof(firstPresetPath) - 1);
        firstPresetPath[sizeof(firstPresetPath) - 1] = '\0';
      }
    }
    // Reject duplicate and case-insensitive colliding paths with bounded
    // memory. The archive is capped at 4096 entries, so the quadratic scan is
    // bounded and avoids retaining attacker-controlled path tables.
    for (mz_uint previous = 0; previous < index; ++previous) {
      char other[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE]{};
      mz_zip_archive_file_stat previousStat{};
      if (mz_zip_reader_get_filename(&archive, previous, other,
                                     sizeof(other)) == 0 ||
          !mz_zip_reader_file_stat(&archive, previous, &previousStat)) {
        result = Result::InvalidPath;
        break;
      }
      const size_t nameLength = std::strlen(name);
      const size_t otherLength = std::strlen(other);
      const bool exactCollision = strcasecmp(name, other) == 0;
      const char *longer = nameLength > otherLength ? name : other;
      const size_t shorterLength =
          nameLength > otherLength ? otherLength : nameLength;
      const bool prefixCollision =
          strncasecmp(name, other, shorterLength) == 0 &&
          longer[shorterLength] == '/' &&
          ((nameLength > otherLength &&
            previousStat.m_is_directory == MZ_FALSE) ||
           (otherLength > nameLength && !directory));
      if (exactCollision || prefixCollision) {
        result = Result::InvalidPath;
        break;
      }
      if (HasCaseFoldedComponentCollision(name, other)) {
        result = Result::InvalidPath;
        break;
      }
    }
    const mz_uint32 unixMode = stat.m_external_attr >> 16;
    const mz_uint32 unixType = unixMode & 0170000U;
    if ((stat.m_version_made_by >> 8) == 3 && unixType != 0 &&
        unixType != 0100000U && unixType != 0040000U) {
      result = Result::UnsupportedArchive;
    }
  }

  if (result == Result::Success) {
    uint64_t freeBytes = 0;
    uint64_t spaceRequired = outputTotal;
    const uint64_t metadataReserve = static_cast<uint64_t>(count) * 512ULL;
    if (spaceRequired > std::numeric_limits<uint64_t>::max() - metadataReserve -
                            1024ULL * 1024ULL) {
      result = Result::InsufficientSpace;
    } else {
      spaceRequired += metadataReserve + 1024ULL * 1024ULL;
    }
    FileSystemPath::PathString parent;
    if (result == Result::Success &&
        (!FileSystemPath::DirName(destinationPath, parent) ||
         !fs->GetFreeSpaceBytes(parent.c_str(), freeBytes))) {
      result = Result::FilesystemError;
    } else if (result == Result::Success &&
               (freeBytes < kMinimumFreeBytes || freeBytes < spaceRequired)) {
      result = Result::InsufficientSpace;
    }
  }

  FileSystemPath::PathString staging;
  if (result == Result::Success) {
    bool foundStage = false;
    for (uint16_t suffix = 0; suffix < 100; ++suffix) {
      char suffixText[3] = {static_cast<char>('0' + suffix / 10),
                            static_cast<char>('0' + suffix % 10), '\0'};
      staging = destinationPath;
      staging.append(".incomplete");
      staging.append(suffixText);
      if (staging.is_truncated()) {
        result = Result::InvalidPath;
        break;
      }
      if (!fs->exists(staging.c_str())) {
        foundStage = true;
        break;
      }
    }
    if (result == Result::Success && !foundStage) {
      result = Result::FilesystemError;
    }
  }
  if (result == Result::Success) {
    for (mz_uint index = 0; index < count; ++index) {
      char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE]{};
      FileSystemPath::PathString outputPath;
      if (!mz_zip_reader_get_filename(&archive, index, name, sizeof(name)) ||
          !Join(staging.c_str(), name, outputPath)) {
        result = Result::InvalidPath;
        break;
      }
    }
  }
  FileSystemPath::PathString markerPath;
  if (result == Result::Success &&
      !Join(staging.c_str(), kIncompleteMarker, markerPath)) {
    result = Result::InvalidPath;
  }
  bool stagingCreated = false;
  if (result == Result::Success &&
      (fs->exists(staging.c_str()) || !fs->makeDir(staging.c_str()))) {
    result = Result::FilesystemError;
  } else if (result == Result::Success) {
    stagingCreated = true;
  }
  if (result == Result::Success) {
    if (!WriteOwnershipMarker(fs, markerPath.c_str())) {
      result = Result::FilesystemError;
    }
  }

  uint64_t extracted = 0;
  for (mz_uint index = 0; result == Result::Success && index < count; ++index) {
    char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE]{};
    mz_zip_archive_file_stat stat{};
    const bool directory = mz_zip_reader_is_file_a_directory(&archive, index);
    if (!mz_zip_reader_get_filename(&archive, index, name, sizeof(name)) ||
        !mz_zip_reader_file_stat(&archive, index, &stat)) {
      result = Result::InvalidArchive;
      break;
    }
    FileSystemPath::PathString outputPath;
    if (!Join(staging.c_str(), name, outputPath)) {
      result = Result::InvalidPath;
      break;
    }
    if (directory) {
      size_t length = outputPath.size();
      while (length > 1 && outputPath[length - 1] == '/') {
        outputPath.resize(length - 1);
        --length;
      }
      if (!fs->makeDir(outputPath.c_str(), true)) {
        result = Result::FilesystemError;
      }
      continue;
    }
    if (!EnsureParentDirs(fs, staging.c_str(), name)) {
      result = Result::FilesystemError;
      break;
    }
    OutputIO output{fs->Open(outputPath.c_str(), "w"),
                    false,
                    false,
                    callback,
                    context,
                    name,
                    &extracted,
                    outputTotal,
                    0};
    if (!output.file) {
      result = Result::FilesystemError;
      break;
    }
    // miniz invokes the output callback in bounded blocks, verifies output
    // length and CRC, and supports entries with data descriptors.
    const size_t workspaceCheckpoint = archiveWorkspaceUsed;
    const bool extractedFile = mz_zip_reader_extract_to_callback(
        &archive, index, WriteOutput, &output, 0);
    // Per-entry input and inflate buffers are gone after this call. Reclaim
    // their monotonic arena space while retaining the central directory.
    archiveWorkspaceUsed = workspaceCheckpoint;
    if (!extractedFile || output.failed || !output.file->Sync()) {
      const auto error = mz_zip_get_last_error(&archive);
      result = extractionCancel.load(std::memory_order_acquire)
                   ? Result::Cancelled
                   : (output.cancelled
                          ? Result::Cancelled
                          : (output.failed || error == MZ_ZIP_FILE_WRITE_FAILED
                                 ? Result::FilesystemError
                                 : (error == MZ_ZIP_ALLOC_FAILED
                                        ? Result::OutOfMemory
                                        : Result::InvalidArchive)));
      break;
    }
    extracted += stat.m_uncomp_size;
    if (callback != nullptr &&
        !callback({"Extracting", name, extracted, outputTotal}, context)) {
      result = Result::Cancelled;
    }
  }

  bool stagingPublished = false;
  if (result == Result::Success) {
    if (fs->exists(destinationPath)) {
      result = Result::ExistingDestination;
    } else if (!fs->RenameDir(staging.c_str(), destinationPath)) {
      result = Result::FilesystemError;
    } else {
      stagingPublished = true;
      FileSystemPath::PathString publishedMarker;
      if (!Join(destinationPath, kIncompleteMarker, publishedMarker) ||
          !fs->DeleteFile(publishedMarker.c_str())) {
        result = Result::FilesystemError;
        if (fs->RenameDir(destinationPath, staging.c_str())) {
          stagingPublished = false;
        } else {
          markerPath = publishedMarker;
        }
      }
    }
  }
  if (result != Result::Success && stagingCreated &&
      (fs->exists(staging.c_str()) ||
       (stagingPublished && fs->exists(destinationPath)))) {
    const char *cleanupRoot =
        fs->exists(staging.c_str()) ? staging.c_str() : destinationPath;
    fs->DeleteFile(markerPath.c_str());
    if (!DeleteTree(fs, cleanupRoot)) {
      FileSystemPath::PathString ownershipMarker;
      if (Join(cleanupRoot, kIncompleteMarker, ownershipMarker)) {
        WriteOwnershipMarker(fs, ownershipMarker.c_str());
      }
    }
  }
  mz_zip_reader_end(&archive);
  return result;
}
} // namespace
