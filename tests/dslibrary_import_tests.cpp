/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "Application/Views/DslibraryExtractor.h"
#include "System/FileSystem/FakeFileSystem.h"
#include "doctest/doctest.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
class FileSystemScope {
public:
  explicit FileSystemScope(FileSystem *fileSystem)
      : previous_(FileSystem::GetInstance()) {
    FileSystem::Install(fileSystem);
  }
  ~FileSystemScope() { FileSystem::Install(previous_); }

private:
  FileSystem *previous_;
};

void put16(std::vector<uint8_t> &bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void put32(std::vector<uint8_t> &bytes, uint32_t value) {
  put16(bytes, static_cast<uint16_t>(value));
  put16(bytes, static_cast<uint16_t>(value >> 16));
}

std::vector<uint8_t> zipOneFile(const char *name,
                                const std::vector<uint8_t> &compressed,
                                uint32_t crc, uint32_t uncompressedSize,
                                uint16_t method, bool descriptor = false) {
  const auto nameLength = static_cast<uint16_t>(std::strlen(name));
  std::vector<uint8_t> bytes;
  put32(bytes, 0x04034b50);
  put16(bytes, 20);
  put16(bytes, descriptor ? 8 : 0);
  put16(bytes, method);
  put16(bytes, 0);
  put16(bytes, 0);
  put32(bytes, descriptor ? 0 : crc);
  put32(bytes, descriptor ? 0 : static_cast<uint32_t>(compressed.size()));
  put32(bytes, descriptor ? 0 : uncompressedSize);
  put16(bytes, nameLength);
  put16(bytes, 0);
  bytes.insert(bytes.end(), name, name + nameLength);
  bytes.insert(bytes.end(), compressed.begin(), compressed.end());
  if (descriptor) {
    put32(bytes, 0x08074b50);
    put32(bytes, crc);
    put32(bytes, static_cast<uint32_t>(compressed.size()));
    put32(bytes, uncompressedSize);
  }
  const uint32_t centralOffset = static_cast<uint32_t>(bytes.size());
  put32(bytes, 0x02014b50);
  put16(bytes, 20);
  put16(bytes, 20);
  put16(bytes, descriptor ? 8 : 0);
  put16(bytes, method);
  put16(bytes, 0);
  put16(bytes, 0);
  put32(bytes, crc);
  put32(bytes, static_cast<uint32_t>(compressed.size()));
  put32(bytes, uncompressedSize);
  put16(bytes, nameLength);
  put16(bytes, 0);
  put16(bytes, 0);
  put16(bytes, 0);
  put16(bytes, 0);
  put32(bytes, 0);
  put32(bytes, 0);
  bytes.insert(bytes.end(), name, name + nameLength);
  const uint32_t centralSize =
      static_cast<uint32_t>(bytes.size()) - centralOffset;
  put32(bytes, 0x06054b50);
  put16(bytes, 0);
  put16(bytes, 0);
  put16(bytes, 1);
  put16(bytes, 1);
  put32(bytes, centralSize);
  put32(bytes, centralOffset);
  put16(bytes, 0);
  return bytes;
}

struct StoredEntry {
  std::string name;
  std::string contents;
  uint32_t reportedSize = UINT32_MAX;
  uint32_t externalAttributes = 0;
  uint16_t method = 0;
};

uint32_t crc32(const std::string &contents) {
  uint32_t crc = 0xFFFFFFFFU;
  for (const unsigned char byte : contents) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

std::vector<uint8_t> zipStoredEntries(const std::vector<StoredEntry> &entries) {
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> localOffsets;
  localOffsets.reserve(entries.size());
  for (const auto &entry : entries) {
    localOffsets.push_back(static_cast<uint32_t>(bytes.size()));
    const auto nameLength = static_cast<uint16_t>(entry.name.size());
    const auto contentsSize = static_cast<uint32_t>(entry.contents.size());
    const auto reportedSize =
        entry.reportedSize == UINT32_MAX ? contentsSize : entry.reportedSize;
    put32(bytes, 0x04034b50);
    put16(bytes, 20);
    put16(bytes, 0);
    put16(bytes, entry.method);
    put16(bytes, 0);
    put16(bytes, 0);
    put32(bytes, crc32(entry.contents));
    put32(bytes, contentsSize);
    put32(bytes, reportedSize);
    put16(bytes, nameLength);
    put16(bytes, 0);
    bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
    bytes.insert(bytes.end(), entry.contents.begin(), entry.contents.end());
  }

  const uint32_t centralOffset = static_cast<uint32_t>(bytes.size());
  for (size_t index = 0; index < entries.size(); ++index) {
    const auto &entry = entries[index];
    const auto nameLength = static_cast<uint16_t>(entry.name.size());
    const auto contentsSize = static_cast<uint32_t>(entry.contents.size());
    const auto reportedSize =
        entry.reportedSize == UINT32_MAX ? contentsSize : entry.reportedSize;
    put32(bytes, 0x02014b50);
    put16(bytes, entry.externalAttributes == 0 ? 20 : (3U << 8) | 20U);
    put16(bytes, 20);
    put16(bytes, 0);
    put16(bytes, entry.method);
    put16(bytes, 0);
    put16(bytes, 0);
    put32(bytes, crc32(entry.contents));
    put32(bytes, contentsSize);
    put32(bytes, reportedSize);
    put16(bytes, nameLength);
    put16(bytes, 0);
    put16(bytes, 0);
    put16(bytes, 0);
    put16(bytes, 0);
    put32(bytes, entry.externalAttributes);
    put32(bytes, localOffsets[index]);
    bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
  }
  const uint32_t centralSize =
      static_cast<uint32_t>(bytes.size()) - centralOffset;
  put32(bytes, 0x06054b50);
  put16(bytes, 0);
  put16(bytes, 0);
  put16(bytes, static_cast<uint16_t>(entries.size()));
  put16(bytes, static_cast<uint16_t>(entries.size()));
  put32(bytes, centralSize);
  put32(bytes, centralOffset);
  put16(bytes, 0);
  return bytes;
}

void addArchive(FakeFileSystem &fs, const std::vector<uint8_t> &archive) {
  fs.AddFile("/instruments/Piano.dslibrary");
  auto file = fs.Open("/instruments/Piano.dslibrary", "w");
  REQUIRE(file);
  REQUIRE(file->Write(archive.data(), static_cast<int>(archive.size()), 1) ==
          static_cast<int>(archive.size()));
}

void installFs(FakeFileSystem &fs) {
  fs.AddDirectory("/instruments");
  FileSystem::Install(&fs);
}

bool cancelExtraction(const DslibraryExtractor::Progress &, void *) {
  return false;
}

bool cancelWhileExtracting(const DslibraryExtractor::Progress &progress,
                           void *) {
  return std::strcmp(progress.stage, "Extracting") != 0;
}
} // namespace

TEST_CASE("DSL library extracts stored content and reports preset path") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const std::string text = "hello";
  const std::vector<uint8_t> data(text.begin(), text.end());
  addArchive(fs, zipOneFile("Piano/Piano.dspreset", data, 0x3610a686, 5, 0));

  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::Success);
  CHECK(DslibraryExtractor::GetPresetSummary(nullptr, 0) == 1);
  uint64_t freeBytes = 0;
  CHECK(fs.GetFreeSpaceBytes("/instruments", freeBytes));
  CHECK(freeBytes >= 256ULL * 1024ULL * 1024ULL);
  auto extracted = fs.Open("/instruments/Piano/Piano/Piano.dspreset", "r");
  REQUIRE(extracted);
  char output[5]{};
  REQUIRE(extracted->Read(output, 5) == 5);
  CHECK(std::string(output, sizeof(output)) == text);
}

TEST_CASE("DSL library extracts nested files and summarizes multiple presets") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const auto archive = zipStoredEntries({
      {"Library/Piano.dspreset", "preset"},
      {"Library/Alternate.dspreset", "preset"},
      {"Library/Samples/C3.wav", "wave"},
      {"Library/Artwork/cover.png", "art"},
  });
  addArchive(fs, archive);

  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::Success);
  CHECK(DslibraryExtractor::GetPresetSummary(nullptr, 0) == 2);
  CHECK(fs.exists("/instruments/Piano/Library/Samples/C3.wav"));
  CHECK(fs.exists("/instruments/Piano/Library/Artwork/cover.png"));
}

TEST_CASE("DSL library retains successful extraction with no presets") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  addArchive(fs, zipStoredEntries({{"Library/README.txt", "library info"}}));

  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::Success);
  CHECK(DslibraryExtractor::GetPresetSummary(nullptr, 0) == 0);
  CHECK(fs.exists("/instruments/Piano/Library/README.txt"));
}

TEST_CASE("DSL library extracts DEFLATE data descriptors") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const std::vector<uint8_t> rawDeflate{0xcb, 0x48, 0xcd, 0xc9,
                                        0xc9, 0x07, 0x00};
  addArchive(fs,
             zipOneFile("Piano.dspreset", rawDeflate, 0x3610a686, 5, 8, true));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::Success);
}

TEST_CASE(
    "DSL library rejects traversal and preserves an existing destination") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const std::vector<uint8_t> data{'x'};
  addArchive(fs, zipOneFile("../escape", data, 0x8cdc1683, 1, 0));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::InvalidPath);
  CHECK_FALSE(fs.exists("/escape"));

  fs.AddDirectory("/instruments/Piano");
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::ExistingDestination);
}

TEST_CASE("DSL library checks CRC and removes failed staging output") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const std::vector<uint8_t> data{'h', 'e', 'l', 'l', 'o'};
  addArchive(fs, zipOneFile("Piano.dspreset", data, 0x00000000, 5, 0));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::InvalidArchive);
  CHECK_FALSE(fs.exists("/instruments/Piano"));
  CHECK_FALSE(fs.exists("/instruments/Piano.incomplete00"));
}

TEST_CASE("DSL library requires the minimum free space before staging") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  fs.SetFreeSpaceBytes(255ULL * 1024ULL * 1024ULL);
  const std::vector<uint8_t> data{'h'};
  addArchive(fs, zipOneFile("Piano.dspreset", data, 0x8cdc1683, 1, 0));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::InsufficientSpace);
  CHECK(fs.exists("/instruments/Piano.dslibrary"));
  CHECK_FALSE(fs.exists("/instruments/Piano.incomplete00"));
}

TEST_CASE("DSL library can be cancelled during archive inspection") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const std::vector<uint8_t> data{'h'};
  addArchive(fs, zipOneFile("Piano.dspreset", data, 0x8cdc1683, 1, 0));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", cancelExtraction,
                                    nullptr) ==
        DslibraryExtractor::Result::Cancelled);
  CHECK_FALSE(fs.exists("/instruments/Piano.incomplete00"));
}

TEST_CASE(
    "DSL library reports cancellation during output and marks failed cleanup") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const std::vector<uint8_t> data{'h', 'e', 'l', 'l', 'o'};
  addArchive(fs, zipOneFile("Piano.dspreset", data, 0x3610a686, 5, 0));
  fs.FailNext(FakeFileSystemOperationType::DeleteDir);

  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", cancelWhileExtracting,
                                    nullptr) ==
        DslibraryExtractor::Result::Cancelled);
  CHECK(fs.exists("/instruments/Piano.incomplete00"));
  CHECK(fs.exists("/instruments/Piano.incomplete00/.dslibrary-incomplete"));
  CHECK(DslibraryExtractor::CleanupIncompleteStage(
      "/instruments/Piano.incomplete00"));
  CHECK_FALSE(fs.exists("/instruments/Piano.incomplete00"));
}

TEST_CASE("DSL library rejects truncated and unsupported ZIP entries") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  const std::vector<uint8_t> data{'h'};
  auto archive = zipOneFile("Piano.dspreset", data, 0x8cdc1683, 1, 0);
  archive.pop_back();
  addArchive(fs, archive);
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::InvalidArchive);
  CHECK_FALSE(fs.exists("/instruments/Piano.incomplete00"));

  fs.DeleteFile("/instruments/Piano.dslibrary");
  addArchive(fs, zipOneFile("Piano.dspreset", data, 0x8cdc1683, 1, 99));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::UnsupportedArchive);
}

TEST_CASE("DSL library rejects path collisions and symlink entries") {
  const auto runArchive = [](const std::vector<StoredEntry> &entries) {
    FakeFileSystem fs;
    FileSystemScope scope(&fs);
    installFs(fs);
    addArchive(fs, zipStoredEntries(entries));
    return DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                       "/instruments/Piano", nullptr, nullptr);
  };
  CHECK(runArchive(
            {{"Folder/One.dspreset", "a"}, {"folder/Two.dspreset", "b"}}) ==
        DslibraryExtractor::Result::InvalidPath);
  CHECK(runArchive({{"/escape.dspreset", "a"}}) ==
        DslibraryExtractor::Result::InvalidPath);
  CHECK(runArchive({{".dslibrary-incomplete", "not a marker"}}) ==
        DslibraryExtractor::Result::InvalidPath);
  CHECK(runArchive({{"link", "", UINT32_MAX, 0120000U << 16}}) ==
        DslibraryExtractor::Result::UnsupportedArchive);
}

TEST_CASE("DSL library enforces entry count, depth, and total output limits") {
  std::vector<StoredEntry> tooManyEntries;
  tooManyEntries.reserve(4097);
  for (uint32_t index = 0; index < 4097; ++index) {
    char name[24]{};
    std::snprintf(name, sizeof(name), "entry%04u", index);
    tooManyEntries.push_back({name, ""});
  }
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  addArchive(fs, zipStoredEntries(tooManyEntries));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::UnsupportedArchive);

  fs.DeleteFile("/instruments/Piano.dslibrary");
  std::string deepPath;
  for (uint8_t depth = 0; depth < 17; ++depth) {
    deepPath += "d/";
  }
  deepPath += "Preset.dspreset";
  addArchive(fs, zipStoredEntries({{deepPath, ""}}));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::InvalidPath);

  fs.DeleteFile("/instruments/Piano.dslibrary");
  addArchive(fs, zipStoredEntries({
                     {"one.bin", "x", 3U * 1024U * 1024U * 1024U, 0, 8},
                     {"two.bin", "x", 3U * 1024U * 1024U * 1024U, 0, 8},
                     {"three.bin", "x", 3U * 1024U * 1024U * 1024U, 0, 8},
                 }));
  CHECK(DslibraryExtractor::Extract("/instruments/Piano.dslibrary",
                                    "/instruments/Piano", nullptr, nullptr) ==
        DslibraryExtractor::Result::UnsupportedArchive);
}

TEST_CASE("DSL preset chooser can page through large directories") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  for (uint32_t index = 0; index < 600; ++index) {
    char path[48]{};
    std::snprintf(path, sizeof(path), "/instruments/Preset%03u.dspreset",
                  index);
    fs.AddFile(path);
  }

  etl::vector<FileSystemEntry, MAX_FILE_INDEX_SIZE> page;
  size_t total = 0;
  REQUIRE(fs.ListDirectoryPage("/instruments", &page, ".dspreset", false, false,
                               0, total));
  CHECK(total == 600);
  CHECK(page.size() == MAX_FILE_INDEX_SIZE);
  CHECK(fs.GetLastListDirectoryStatus() == DirectoryListStatus::Ok);
  REQUIRE(fs.ListDirectoryPage("/instruments", &page, ".dspreset", false, false,
                               255, total));
  CHECK(page.size() == MAX_FILE_INDEX_SIZE);
  CHECK(page[0].name == "Preset255.dspreset");
  REQUIRE(fs.ListDirectoryPage("/instruments", &page, ".dspreset", false, false,
                               510, total));
  CHECK(page.size() == 90);
  CHECK(page[89].name == "Preset599.dspreset");
}

TEST_CASE("Recovery cleanup requires the extractor ownership marker") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  installFs(fs);
  fs.AddDirectory("/instruments/Unknown.incomplete00");
  fs.AddFile("/instruments/Unknown.incomplete00/.dslibrary-incomplete",
             "not-owned");
  CHECK_FALSE(DslibraryExtractor::CleanupIncompleteStage(
      "/instruments/Unknown.incomplete00"));
  CHECK(fs.exists("/instruments/Unknown.incomplete00"));

  fs.AddDirectory("/instruments/Owned.incomplete01");
  fs.AddFile("/instruments/Owned.incomplete01/.dslibrary-incomplete",
             "picoTracker dslibrary staging v1");
  fs.AddFile("/instruments/Owned.incomplete01/partial.bin", "partial");
  for (uint32_t index = 0; index < 300; ++index) {
    char path[80]{};
    std::snprintf(path, sizeof(path),
                  "/instruments/Owned.incomplete01/%03u.bin", index);
    fs.AddFile(path, "partial");
  }
  CHECK(DslibraryExtractor::CleanupIncompleteStage(
      "/instruments/Owned.incomplete01"));
  CHECK_FALSE(fs.exists("/instruments/Owned.incomplete01"));
}
