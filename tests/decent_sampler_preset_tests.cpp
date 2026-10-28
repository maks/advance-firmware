/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Application/Instruments/DecentSamplerPreset.h"
#include "Application/Instruments/WavHeader.h"
#include "System/FileSystem/FakeFileSystem.h"
#include "doctest/doctest.h"

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

void addPcm16Wav(FakeFileSystem &fileSystem, const char *path) {
  fileSystem.AddFile(path);
  auto file = fileSystem.Open(path, "w");
  REQUIRE(file);
  REQUIRE(WavHeaderWriter::WriteHeader(file.get(), 44100, 1, 16));
  const int16_t samples[] = {0, 4096, -4096, 0};
  REQUIRE(file->Write(samples, sizeof(int16_t), 4) == sizeof(samples));
  REQUIRE(WavHeaderWriter::UpdateFileSize(file.get(), 4, 1, 2));
}
} // namespace

TEST_CASE("DecentSampler imports one supported key and velocity zone") {
  FakeFileSystem fileSystem;
  FileSystemScope fileSystemScope(&fileSystem);
  fileSystem.AddDirectory("/instruments");
  fileSystem.AddDirectory("/instruments/Samples");
  addPcm16Wav(fileSystem, "/instruments/Samples/C4.wav");
  fileSystem.AddFile(
      "/instruments/Guitar.dspreset",
      "<DecentSampler><groups><group><sample path=\"Samples/C4.wav\" "
      "rootNote=\"C4\" loNote=\"48\" hiNote=\"72\" loVel=\"1\" "
      "hiVel=\"127\"/></group></groups></DecentSampler>");

  DecentSamplerPreset preset;
  const auto result =
      LoadDecentSamplerPreset("/instruments/Guitar.dspreset", preset);
  REQUIRE(result);
  REQUIRE(preset.zoneCount_ == 1);
  CHECK(preset.zones_[0].rootNote_ == 60);
  CHECK(preset.zones_[0].lowNote_ == 48);
  CHECK(preset.zones_[0].highNote_ == 72);
  CHECK(preset.zones_[0].lowVelocity_ == 1);
  CHECK(preset.zones_[0].highVelocity_ == 127);
  CHECK(std::strcmp(preset.zones_[0].path_, "/instruments/Samples/C4.wav") ==
        0);
}

TEST_CASE("DecentSampler keeps preset order for overlapping mapping zones") {
  FakeFileSystem fileSystem;
  FileSystemScope fileSystemScope(&fileSystem);
  fileSystem.AddDirectory("/instruments");
  addPcm16Wav(fileSystem, "/instruments/C4.wav");
  fileSystem.AddFile("/instruments/Overlap.dspreset",
                     "<DecentSampler><groups><group>"
                     "<sample path=\"C4.wav\" rootNote=\"C4\" loNote=\"60\" "
                     "hiNote=\"60\" loVel=\"1\" hiVel=\"127\"/>"
                     "<sample path=\"C4.wav\" rootNote=\"C4\" loNote=\"60\" "
                     "hiNote=\"60\" loVel=\"64\" hiVel=\"127\"/>"
                     "</group></groups></DecentSampler>");

  DecentSamplerPreset preset;
  const auto result =
      LoadDecentSamplerPreset("/instruments/Overlap.dspreset", preset);
  REQUIRE(result);
  REQUIRE(preset.zoneCount_ == 2);
  CHECK(preset.zones_[0].lowVelocity_ == 1);
  CHECK(preset.zones_[1].lowVelocity_ == 64);
}

TEST_CASE("DecentSampler rejects malformed XML before opening its samples") {
  FakeFileSystem fileSystem;
  FileSystemScope fileSystemScope(&fileSystem);
  fileSystem.AddDirectory("/instruments");
  fileSystem.AddFile("/instruments/Broken.dspreset",
                     "<DecentSampler><groups></other></DecentSampler>");

  DecentSamplerPreset preset;
  const auto result =
      LoadDecentSamplerPreset("/instruments/Broken.dspreset", preset);
  CHECK_FALSE(result);
  CHECK(result.error_ == DecentSamplerPresetError::MalformedXml);
}
