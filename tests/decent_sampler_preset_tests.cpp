/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Application/Instruments/DecentSamplerPreset.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Instruments/WavHeader.h"
#include "System/FileSystem/FakeFileSystem.h"
#include "TestStreamingSamplePool.h"
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

namespace {
void addAiff(FakeFileSystem &fs, const char *path, bool truncated = false) {
  // Stereo 24-bit, two frames at 44100 Hz. Includes an odd-length chunk
  // and a nonzero SSND offset to exercise chunk traversal.
  const uint8_t bytes[] = {
      'F',  'O',  'R',  'M',  0,    0,    0, 74,   'A', 'I', 'F',  'F',
      'J',  'U',  'N',  'K',  0,    0,    0, 1,    7,   0,   'C',  'O',
      'M',  'M',  0,    0,    0,    18,   0, 2,    0,   0,   0,    2,
      0,    24,   0x40, 0x0e, 0xac, 0x44, 0, 0,    0,   0,   0,    0,
      'S',  'S',  'N',  'D',  0,    0,    0, 26,   0,   0,   0,    6,
      0,    0,    0,    0,    0,    0,    0, 0,    0,   0,   0x7f, 0xff,
      0xff, 0x80, 0,    0,    0x40, 0,    0, 0xc0, 0,   0};
  fs.AddFile(path);
  auto file = fs.Open(path, "w");
  REQUIRE(file);
  const uint32_t size = sizeof(bytes) - (truncated ? 1 : 0);
  REQUIRE(file->Write(bytes, 1, size) == size);
}
} // namespace

TEST_CASE("DecentSampler accepts stereo 24-bit AIFF and decodes PCM") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  fs.AddDirectory("/instruments");
  addAiff(fs, "/instruments/piano.aif");
  fs.AddFile("/instruments/Piano.dspreset",
             "<DecentSampler><groups><group><sample path=\"piano.aif\" "
             "rootNote=\"60\"/></group></groups></DecentSampler>");
  DecentSamplerPreset preset;
  REQUIRE(LoadDecentSamplerPreset("/instruments/Piano.dspreset", preset));
  REQUIRE(preset.zoneCount_ == 1);
  CHECK(preset.zones_[0].sampleRate_ == 44100);
  CHECK(preset.zones_[0].channelCount_ == 2);
  CHECK(preset.zones_[0].frameCount_ == 2);
  WavFile file;
  REQUIRE(file.Open("/instruments/piano.aif"));
  float samples[4]{};
  uint32_t count = 0;
  REQUIRE(file.ReadFloat(samples, 4, &count));
  REQUIRE(count == 4);
  CHECK(samples[0] == doctest::Approx(8388607.0 / 8388608.0));
  CHECK(samples[1] == -1.0f);
  CHECK(samples[2] == 0.5f);
  CHECK(samples[3] == -0.5f);
  REQUIRE(file.Rewind());
  int16_t pcm[6]{};
  REQUIRE(file.Read(pcm, sizeof(pcm), &count));
  CHECK(count == 8);
  CHECK(pcm[0] == 32767);
  CHECK(pcm[1] == -32768);
  CHECK(pcm[2] == 16384);
  CHECK(pcm[3] == -16384);
  REQUIRE(file.GetBuffer(1, 1));
  const auto *buffer = static_cast<const int16_t *>(file.GetSampleBuffer(-1));
  CHECK(buffer[0] == 16384);
  CHECK(buffer[1] == -16384);
  TestStreamingSamplePool pool;
  SampleConversionErrorCode error{};
  REQUIRE(pool.ConvertSampleFile("/instruments/piano.aif",
                                 "/instruments/converted.wav", error));
  WavFile converted;
  REQUIRE(converted.Open("/instruments/converted.wav"));
  CHECK(converted.GetSize(-1) == 2);
  CHECK(converted.GetChannelCount(-1) == 2);
  REQUIRE(converted.GetBuffer(0, 2));
  const auto *convertedPcm =
      static_cast<const int16_t *>(converted.GetSampleBuffer(-1));
  CHECK(convertedPcm[0] == 32767);
  CHECK(convertedPcm[1] == -32768);
  CHECK(convertedPcm[2] == 16384);
  CHECK(convertedPcm[3] == -16384);
}

TEST_CASE("AIFF rejects truncated sample data") {
  FakeFileSystem fs;
  FileSystemScope scope(&fs);
  fs.AddDirectory("/instruments");
  addAiff(fs, "/instruments/truncated.aif", true);
  WavFile file;
  CHECK_FALSE(file.Open("/instruments/truncated.aif"));
}
