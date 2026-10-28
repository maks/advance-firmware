/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Adapters/wasm/system/WasmSamplePool.h"
#include "Application/Instruments/MSamplerInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/WavHeader.h"
#include "System/FileSystem/FakeFileSystem.h"
#include "doctest/doctest.h"
#include <filesystem>
#include <string>

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

class SamplePoolScope {
public:
  explicit SamplePoolScope(SamplePool *pool)
      : previous_(SamplePool::GetInstance()) {
    SamplePool::Install(pool);
  }
  ~SamplePoolScope() { SamplePool::Install(previous_); }

private:
  SamplePool *previous_;
};

void addPcm16Wav(FakeFileSystem &fileSystem, const char *path) {
  fileSystem.AddFile(path);
  auto file = fileSystem.Open(path, "w");
  REQUIRE(file);
  REQUIRE(WavHeaderWriter::WriteHeader(file.get(), 44100, 1, 16));
  const int16_t samples[] = {8000, 12000, -4000, -8000};
  REQUIRE(file->Write(samples, sizeof(int16_t), 4) == sizeof(samples));
  REQUIRE(WavHeaderWriter::UpdateFileSize(file.get(), 4, 1, 2));
}
} // namespace

TEST_CASE("MSAMPLER plays the selected WAV zone from its attack cache") {
  FakeFileSystem fileSystem;
  FileSystemScope fileSystemScope(&fileSystem);
  WasmSamplePool samplePool;
  SamplePoolScope samplePoolScope(&samplePool);
  fileSystem.AddDirectory("/instruments");
  addPcm16Wav(fileSystem, "/instruments/C4.wav");
  fileSystem.AddFile(
      "/instruments/Test.dspreset",
      "<DecentSampler><groups><group><sample path=\"C4.wav\" "
      "rootNote=\"C4\" loNote=\"60\" hiNote=\"60\" "
      "loVel=\"1\" hiVel=\"127\"/></group></groups></DecentSampler>");

  DecentSamplerPreset preset;
  const auto parsed =
      LoadDecentSamplerPreset("/instruments/Test.dspreset", preset);
  REQUIRE(parsed);

  MSamplerInstrument instrument;
  REQUIRE(instrument.LoadPreset("/instruments/Test.dspreset"));
  REQUIRE(instrument.StartWithVelocity(0, 60, 100));

  AudioSample output[8]{};
  REQUIRE(instrument.Render(0, output, 4, false));
  CHECK(output[0] == doctest::Approx((8000.0f / 32768.0f) * (192.0f / 255.0f)));
  CHECK(output[1] == output[0]);
  CHECK(output[2] ==
        doctest::Approx((12000.0f / 32768.0f) * (192.0f / 255.0f)));

  instrument.Release(0);
  REQUIRE(instrument.Render(0, output, 4, false));
  CHECK(output[0] != 0.0f);
}

TEST_CASE("MSAMPLER loads and plays the Modern Acoustic Guitar V2 preset") {
  const std::filesystem::path instrumentFolder(
      "refs/Modern Acoustic Guitar V2");
  if (!std::filesystem::exists(instrumentFolder /
                               "Modern Acoustic Guitar.dspreset")) {
    MESSAGE(
        "Modern Acoustic Guitar V2 reference assets are not present; skipping");
    return;
  }

  WasmFileSystem fileSystem(".");
  FileSystemScope fileSystemScope(&fileSystem);
  WasmSamplePool samplePool;
  SamplePoolScope samplePoolScope(&samplePool);
  DecentSamplerPreset preset;
  const auto parsed = LoadDecentSamplerPreset(
      "/refs/Modern Acoustic Guitar V2/Modern Acoustic Guitar.dspreset",
      preset);
  INFO("parse result: " << std::string(parsed.message_));
  INFO("parse line: " << parsed.line_);
  REQUIRE(parsed);
  MSamplerInstrument instrument;
  REQUIRE(instrument.LoadPreset(
      "/refs/Modern Acoustic Guitar V2/Modern Acoustic Guitar.dspreset"));

  bool started = false;
  for (uint8_t velocity : {uint8_t{16}, uint8_t{60}, uint8_t{110}}) {
    for (uint16_t note = 0; note <= 127 && !started; ++note) {
      started =
          instrument.StartWithVelocity(0, static_cast<uint8_t>(note), velocity);
    }
    if (started) {
      break;
    }
  }
  REQUIRE(started);

  AudioSample output[2048]{};
  bool audible = false;
  bool completed = false;
  for (uint8_t block = 0; block < 32; ++block) {
    REQUIRE(instrument.Render(0, output, 1024, false));
    for (AudioSample sample : output) {
      audible = audible || sample != 0.0f;
    }
    uint32_t position = 0;
    if (!instrument.GetPlaybackPosition(0, position)) {
      completed = true;
      break;
    }
  }
  CHECK(audible);
  CHECK(completed);
}
