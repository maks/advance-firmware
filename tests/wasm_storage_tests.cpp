/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Adapters/wasm/filesystem/WasmFile.h"
#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Adapters/wasm/filesystem/WasmStorageBridge.h"
#include <atomic>
#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::atomic<uint32_t> gStorageMutationCount{0};

void TestStorageMutationNotifier() {
  gStorageMutationCount.fetch_add(1, std::memory_order_relaxed);
}

struct TestDirGuard {
  fs::path root;

  explicit TestDirGuard(const std::string &subDir) {
    root = fs::temp_directory_path() / subDir;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
  }

  ~TestDirGuard() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};
} // namespace

TEST_CASE("WasmFileSystem path resolution, root containment and traversal "
          "rejection") {
  TestDirGuard guard("picotracker_wasm_fs_test_containment");
  WasmFileSystem wasmFs(guard.root.string());

  CHECK(wasmFs.exists("/"));

  // Check creating and checking paths inside root
  CHECK(wasmFs.makeDir("/projects/song1", true));
  CHECK(wasmFs.exists("/projects/song1"));

  // Absolute path that already has guard.root as prefix should resolve without
  // double-prefixing
  std::string fullInternalPath = (guard.root / "projects/song1").string();
  CHECK(wasmFs.exists(fullInternalPath.c_str()));

  // Path traversal attempts outside root must fail
  CHECK_FALSE(wasmFs.exists("/../outside.txt"));
  CHECK_FALSE(wasmFs.exists("/projects/../../outside.txt"));
  CHECK_FALSE(wasmFs.makeDir("/../escaped_dir", true));

  auto invalidHandle = wasmFs.Open("/../../../etc/escaped.dat", "w");
  CHECK_FALSE(invalidHandle);
}

TEST_CASE("WasmFileSystem file write, read, sync, copy, move and delete with "
          "mutation notification") {
  TestDirGuard guard("picotracker_wasm_fs_test_operations");
  WasmFileSystem wasmFs(guard.root.string());

  gStorageMutationCount.store(0);
  WasmStorage_SetMutationNotifierForTesting(TestStorageMutationNotifier);

  // 1. Create file and write content
  {
    auto file = wasmFs.Open("/projects/song1/lgpt.dat", "w");
    REQUIRE(file);
    const char data[] = "PICO_TRACKER_DATA_BLOCK";
    const int written = file->Write(data, 1, sizeof(data));
    CHECK(written == sizeof(data));
    CHECK(file->Sync());
    // Sync on dirty file triggers mutation
    CHECK(gStorageMutationCount.load() >= 1);
  }
  uint32_t countAfterWrite = gStorageMutationCount.load();
  CHECK(countAfterWrite >= 1);

  // 2. Read back
  {
    auto file = wasmFs.Open("/projects/song1/lgpt.dat", "r");
    REQUIRE(file);
    char buf[64] = {0};
    const int readBytes = file->Read(buf, sizeof(buf));
    CHECK(readBytes > 0);
    CHECK(std::string(buf) == "PICO_TRACKER_DATA_BLOCK");
  }

  // 3. Copy file
  {
    const uint32_t countBefore = gStorageMutationCount.load();
    CHECK(wasmFs.CopyFile("/projects/song1/lgpt.dat",
                          "/projects/song1/backup.dat"));
    CHECK(wasmFs.exists("/projects/song1/backup.dat"));
    CHECK(gStorageMutationCount.load() > countBefore);
  }

  // 4. Move file
  {
    const uint32_t countBefore = gStorageMutationCount.load();
    CHECK(wasmFs.MoveFile("/projects/song1/backup.dat",
                          "/projects/song1/renamed.dat"));
    CHECK_FALSE(wasmFs.exists("/projects/song1/backup.dat"));
    CHECK(wasmFs.exists("/projects/song1/renamed.dat"));
    CHECK(gStorageMutationCount.load() > countBefore);
  }

  // 5. Delete file
  {
    const uint32_t countBefore = gStorageMutationCount.load();
    CHECK(wasmFs.DeleteFile("/projects/song1/renamed.dat"));
    CHECK_FALSE(wasmFs.exists("/projects/song1/renamed.dat"));
    CHECK(gStorageMutationCount.load() > countBefore);
  }

  // 6. Delete directory
  {
    CHECK(wasmFs.DeleteFile("/projects/song1/lgpt.dat"));
    const uint32_t countBefore = gStorageMutationCount.load();
    CHECK(wasmFs.DeleteDir("/projects/song1"));
    CHECK_FALSE(wasmFs.exists("/projects/song1"));
    CHECK(gStorageMutationCount.load() > countBefore);
  }

  WasmStorage_SetMutationNotifierForTesting(nullptr);
}

TEST_CASE("WasmFileSystem directory listing, filtering, and hidden entry "
          "handling") {
  TestDirGuard guard("picotracker_wasm_fs_test_listing");
  WasmFileSystem wasmFs(guard.root.string());

  CHECK(wasmFs.makeDir("/samples", true));
  CHECK(wasmFs.makeDir("/samples/drums", true));
  {
    auto f1 = wasmFs.Open("/samples/kick.wav", "w");
    REQUIRE(f1);
    f1->Write("kick", 1, 4);
  }
  {
    auto f2 = wasmFs.Open("/samples/snare.WAV", "w");
    REQUIRE(f2);
    f2->Write("snare", 1, 5);
  }
  {
    auto f3 = wasmFs.Open("/samples/synth.raw", "w");
    REQUIRE(f3);
    f3->Write("synth", 1, 5);
  }
  {
    auto f4 = wasmFs.Open("/samples/.hidden.wav", "w");
    REQUIRE(f4);
    f4->Write("hidden", 1, 6);
  }

  // Listing all excluding hidden
  {
    etl::vector<FileSystemEntry, 16> entries;
    CHECK(wasmFs.ListDirectory("/samples", &entries, nullptr, false, false));
    // drums (dir), kick.wav (file), snare.WAV (file), synth.raw (file) = 4
    // items
    CHECK(entries.size() == 4);
    CHECK(wasmFs.GetLastListDirectoryStatus() == DirectoryListStatus::Ok);

    bool foundDrums = false;
    bool foundKick = false;
    bool foundSnare = false;
    bool foundHidden = false;

    for (const auto &e : entries) {
      if (std::string(e.name.c_str()) == "drums") {
        foundDrums = true;
        CHECK(e.type == PFT_DIR);
      }
      if (std::string(e.name.c_str()) == "kick.wav") {
        foundKick = true;
        CHECK(e.type == PFT_FILE);
        CHECK(e.size == 4);
      }
      if (std::string(e.name.c_str()) == "snare.WAV") {
        foundSnare = true;
        CHECK(e.type == PFT_FILE);
        CHECK(e.size == 5);
      }
      if (std::string(e.name.c_str()) == ".hidden.wav") {
        foundHidden = true;
      }
    }
    CHECK(foundDrums);
    CHECK(foundKick);
    CHECK(foundSnare);
    CHECK_FALSE(foundHidden);
  }

  // Listing with filter (case-insensitive ".wav")
  {
    etl::vector<FileSystemEntry, 16> entries;
    CHECK(wasmFs.ListDirectory("/samples", &entries, ".wav", false, false));
    bool foundKick = false;
    bool foundSnare = false;
    bool foundSynth = false;

    for (const auto &e : entries) {
      if (std::string(e.name.c_str()) == "kick.wav")
        foundKick = true;
      if (std::string(e.name.c_str()) == "snare.WAV")
        foundSnare = true;
      if (std::string(e.name.c_str()) == "synth.raw")
        foundSynth = true;
    }
    CHECK(foundKick);
    CHECK(foundSnare);
    CHECK_FALSE(foundSynth);
  }

  // Listing directories only
  {
    etl::vector<FileSystemEntry, 16> entries;
    CHECK(wasmFs.ListDirectory("/samples", &entries, nullptr, true, false));
    CHECK(entries.size() == 1);
    CHECK(std::string(entries[0].name.c_str()) == "drums");
    CHECK(entries[0].type == PFT_DIR);
  }

  // Listing with includeHidden = true
  {
    etl::vector<FileSystemEntry, 16> entries;
    CHECK(wasmFs.ListDirectory("/samples", &entries, nullptr, false, true));
    bool foundHidden = false;
    for (const auto &e : entries) {
      if (std::string(e.name.c_str()) == ".hidden.wav") {
        foundHidden = true;
        CHECK(e.isHidden);
      }
    }
    CHECK(foundHidden);
  }
}
