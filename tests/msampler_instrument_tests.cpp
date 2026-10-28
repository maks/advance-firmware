/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Adapters/wasm/system/WasmSamplePool.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/MSamplerInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/WavHeader.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "System/FileSystem/FakeFileSystem.h"
#include "doctest/doctest.h"
#include <filesystem>
#include <memory>
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

TEST_CASE("MSAMPLER bank restores a playable preset saved before its type") {
  FakeFileSystem fileSystem;
  FileSystemScope fileSystemScope(&fileSystem);
  WasmSamplePool samplePool;
  SamplePoolScope samplePoolScope(&samplePool);
  fileSystem.AddDirectory("/instruments");
  addPcm16Wav(fileSystem, "/instruments/C4.wav");
  fileSystem.AddFile("/instruments/Test.dspreset",
                     "<DecentSampler><groups><group><sample path=\"C4.wav\" "
                     "rootNote=\"60\"/></group></groups></DecentSampler>");
  auto savedFile = fileSystem.Open("/bank.xml", "w");
  REQUIRE(savedFile);
  tinyxml2::XMLPrinter printer(savedFile.get());
  printer.OpenElement("BANK");
  {
    auto bank = std::make_unique<InstrumentBank>();
    REQUIRE(bank->GetNextAndAssignID(IT_MSAMPLER, 0) != NO_MORE_INSTRUMENT);
    auto *instrument =
        static_cast<MSamplerInstrument *>(bank->GetInstrument(0));
    REQUIRE(instrument->LoadPreset("/instruments/Test.dspreset"));
    bank->SaveContent(&printer);
  }
  printer.CloseElement();
  savedFile.reset();
  auto inspect = fileSystem.Open("/bank.xml", "r");
  std::string xml;
  for (int32_t c = inspect->GetC(); c != EOF; c = inspect->GetC()) {
    xml += static_cast<char>(c);
  }
  inspect.reset();
  // The shared audio test harness stubs I_Instrument::SaveContent. Supply
  // the TYPE attribute that the real base writer appends after PRESET.
  const auto headerEnd = xml.find("/>");
  REQUIRE(headerEnd != std::string::npos);
  xml.insert(headerEnd, " TYPE=\"MSAMPLER\"");
  fileSystem.AddFile("/bank.xml", xml.c_str());
  INFO(xml);
  PersistencyDocument doc;
  REQUIRE(doc.Load("/bank.xml"));
  REQUIRE(doc.FirstChild());
  auto restored = std::make_unique<InstrumentBank>();
  restored->RestoreContent(&doc);
  REQUIRE_FALSE(doc.HadError());
  REQUIRE(restored->GetInstrument(0)->GetType() == IT_MSAMPLER);
  auto *instrument =
      static_cast<MSamplerInstrument *>(restored->GetInstrument(0));
  CHECK(std::string(instrument->GetPresetPath()) ==
        "/instruments/Test.dspreset");
  REQUIRE(instrument->StartWithVelocity(0, 60, 100));
  AudioSample output[8]{};
  REQUIRE(instrument->Render(0, output, 4, false));
  CHECK(output[0] != 0.0f);
}

TEST_CASE("MSAMPLER interpolates through attack and streamed page boundaries") {
  for (uint8_t channels : {uint8_t{1}, uint8_t{2}}) {
    class StreamFailureFileSystem : public FakeFileSystem {
    public:
      bool failReads_ = false;
      FileHandle Open(const char *path, const char *mode) override {
        if (failReads_ && mode[0] == 'r') {
          return {};
        }
        return FakeFileSystem::Open(path, mode);
      }
    } fileSystem;
    FileSystemScope fsScope(&fileSystem);
    WasmSamplePool samplePool;
    SamplePoolScope poolScope(&samplePool);
    fileSystem.AddDirectory("/instruments");
    const uint32_t frames = 60000;
    auto sampleAt = [](uint32_t frame, uint8_t side) {
      return static_cast<int16_t>((frame * 37U + side * 999U) % 30000U - 15000);
    };
    std::vector<int16_t> pcm(frames * channels);
    for (uint32_t frame = 0; frame < frames; ++frame) {
      for (uint8_t side = 0; side < channels; ++side) {
        pcm[frame * channels + side] = sampleAt(frame, side);
      }
    }
    fileSystem.AddFile("/instruments/Long.wav");
    {
      auto file = fileSystem.Open("/instruments/Long.wav", "w");
      REQUIRE(file);
      REQUIRE(WavHeaderWriter::WriteHeader(file.get(), 44100, channels, 16));
      REQUIRE(file->Write(pcm.data(), sizeof(int16_t), pcm.size()) ==
              static_cast<int32_t>(pcm.size() * sizeof(int16_t)));
      REQUIRE(WavHeaderWriter::UpdateFileSize(file.get(), frames, channels, 2));
    }
    // 64 disjoint zones limit each attack to 48 KiB, exercising actual stream
    // pages with small reproducible assets instead of multi-megabyte fixtures.
    std::string preset = "<DecentSampler><groups><group>";
    for (uint8_t note = 0; note < 64; ++note) {
      preset += "<sample path=\"Long.wav\" rootNote=\"60\" loNote=\"" +
                std::to_string(note) + "\" hiNote=\"" + std::to_string(note) +
                "\" start=\"13\"/>";
    }
    preset += "</group></groups></DecentSampler>";
    fileSystem.AddFile("/instruments/Long.dspreset", preset.c_str());
    MSamplerInstrument instrument;
    REQUIRE(instrument.LoadPreset("/instruments/Long.dspreset"));
    for (float speed : {0.5f, 1.0f, 4.0f}) {
      INFO("channels=" << static_cast<uint32_t>(channels)
                       << " speed=" << speed);
      REQUIRE(instrument.StartWithVelocity(0, 60, 100));
      instrument.SetPitchMultiplier(0, speed);
      float position = 13;
      AudioSample output[514]{};
      while (position < frames - 2) {
        const int32_t count = std::min<int32_t>(
            257, static_cast<int32_t>((frames - 2 - position) / speed));
        if (count == 0) {
          break;
        }
        REQUIRE(instrument.Render(0, output, count, false));
        for (int32_t index = 0; index < count; ++index) {
          const uint32_t frame = static_cast<uint32_t>(position);
          const float fraction = position - frame;
          for (uint8_t side = 0; side < 2; ++side) {
            const uint8_t sourceSide = channels == 1 ? 0 : side;
            const float first = sampleAt(frame, sourceSide) / 32768.0f;
            const float second = sampleAt(frame + 1, sourceSide) / 32768.0f;
            const float expected =
                (first + (second - first) * fraction) * (192.0f / 255.0f);
            REQUIRE(output[index * 2 + side] == doctest::Approx(expected));
          }
          position += speed;
        }
      }
      instrument.Stop(0);
    }
    // Failed backing-file reads must fade and retire the voice, rather than
    // leave its cursor frozen forever at the end of its cached attack.
    fileSystem.failReads_ = true;
    REQUIRE(instrument.StartWithVelocity(0, 60, 100));
    AudioSample failedOutput[514]{};
    uint32_t renderedFrames = 0;
    uint32_t position = 0;
    while (instrument.GetPlaybackPosition(0, position) &&
           renderedFrames < frames) {
      REQUIRE(instrument.Render(0, failedOutput, 257, false));
      renderedFrames += 257;
    }
    CHECK(renderedFrames < frames);
    CHECK_FALSE(instrument.GetPlaybackPosition(0, position));
    CHECK(failedOutput[513] == 0.0f);
  }
}
