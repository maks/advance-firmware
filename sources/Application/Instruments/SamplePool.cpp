/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "SamplePool.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Utils/DrawUtils.h"
#include "Externals/SRC/common.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/string_stream.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include "System/FileSystem/PathUtils.h"
#include "System/io/Status.h"
#include "WavHeader.h"
#include <cstdint>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <utility>

namespace {

using SampleLoadEntries = etl::vector<FileSystemEntry, MAX_FILE_INDEX_SIZE>;

static constexpr uint32_t kImportOutputSampleRate = 44100;
static constexpr uint32_t kImportOutputBytesPerSample = sizeof(int16_t);

#if defined(HOST_TEST)
#define SAMPLE_LOAD_ENTRIES_ATTRIBUTES __attribute__((aligned(32)))
#else
#define SAMPLE_LOAD_ENTRIES_ATTRIBUTES                                         \
  __attribute__((section(".SDRAM1_NC"))) __attribute__((aligned(32)))
#endif

alignas(SampleLoadEntries) static unsigned char sampleLoadEntriesBuffer[sizeof(
    SampleLoadEntries)] SAMPLE_LOAD_ENTRIES_ATTRIBUTES;

#undef SAMPLE_LOAD_ENTRIES_ATTRIBUTES

SampleLoadEntries &GetSampleLoadEntries() {
  static SampleLoadEntries *entries = nullptr;
  if (entries == nullptr) {
    entries = new (sampleLoadEntriesBuffer) SampleLoadEntries();
  }
  return *entries;
}

bool extractSampleFilename(
    const char *path,
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &sampleFilename) {
  FileSystemPath::PathString baseName;
  if (!FileSystemPath::BaseName(path, baseName)) {
    sampleFilename.clear();
    return false;
  }

  sampleFilename = baseName.c_str();
  return !sampleFilename.empty();
}

bool buildProjectSamplesPath(const char *projectName,
                             FileSystemPath::PathString &projectSamplesPath) {
  FileSystemPath::PathString projectPath;
  if (!FileSystemPath::JoinPath(PROJECTS_DIR, projectName, projectPath)) {
    return false;
  }

  return FileSystemPath::JoinPath(projectPath.c_str(), PROJECT_SAMPLES_DIR,
                                  projectSamplesPath);
}

SRC_STATE *createImportResampler(int preferredConverter, int32_t channelCount,
                                 int *outError) {
  int srcError = SRC_ERR_NO_ERROR;
  SRC_STATE *resampler = src_new(preferredConverter, channelCount, &srcError);
  if (resampler || preferredConverter == SRC_LINEAR ||
      srcError != SRC_ERR_BAD_CONVERTER) {
    if (outError) {
      *outError = srcError;
    }
    return resampler;
  }

  Trace::Error("Preferred resampler %d unavailable, falling back to linear",
               preferredConverter);

  srcError = SRC_ERR_NO_ERROR;
  resampler = src_new(SRC_LINEAR, channelCount, &srcError);
  if (outError) {
    *outError = srcError;
  }
  return resampler;
}

} // namespace

SamplePool::SamplePool() : Observable(&observers_) {
  count_ = 0;
  for (int i = 0; i < MAX_SAMPLES; i++) {
    names_[i] = nameStore_[i];
    nameStore_[i][0] = '\0';
  };
};

SamplePool::~SamplePool() {
  for (int i = 0; i < MAX_SAMPLES; i++) {
    wav_[i].Close();
  };
};

void SamplePool::updateStatus(uint32_t index, uint32_t total,
                              const char *message) {
  progressBar_t progressBar;
  uint32_t percentage = (total > 0) ? (index * 100U) / total : 100U;
  fillProgressBar(index, total, &progressBar);
  Status::SetMultiLine("%s %.19s" char_indicator_ellipsis_s " \n \n %s %3d%%",
                       message, importName, progressBar,
                       static_cast<int>(percentage));
};

void SamplePool::Load(const char *projectName) {
  auto fs = FileSystem::GetInstance();
  auto &sampleLoadEntries = GetSampleLoadEntries();
  FileSystemPath::PathString projectSamplesPath;
  if (!buildProjectSamplesPath(projectName, projectSamplesPath)) {
    Trace::Error("SAMPLEPOOL: Failed to resolve project samples path for %s",
                 projectName ? projectName : "(null)");
    return;
  }

  sampleLoadEntries.clear();
  if (!fs->ListDirectory(projectSamplesPath.c_str(), &sampleLoadEntries, ".wav",
                         false)) {
    Trace::Error("SAMPLEPOOL: Failed to list project samples: %s",
                 projectSamplesPath.c_str());
    return;
  }

  const uint32_t totalSamples = sampleLoadEntries.size();

  // store for ui updates
  importCount = totalSamples;

  for (uint32_t i = 0; i < totalSamples; i++) {
    importIndex = i;
    importName = sampleLoadEntries[i].name.c_str();

    if (sampleLoadEntries[i].type != PFT_FILE) {
      continue;
    }

    if (sampleLoadEntries[i].name.size() > MAX_INSTRUMENT_FILENAME_LENGTH) {
      Trace::Error(
          "SAMPLEPOOL: Sample filename exceeds maximum length: %s (%zu > %d)",
          sampleLoadEntries[i].name.c_str(), sampleLoadEntries[i].name.size(),
          MAX_INSTRUMENT_FILENAME_LENGTH);
      continue;
    }

    FileSystemPath::PathString samplePath;
    if (!FileSystemPath::JoinPath(projectSamplesPath.c_str(),
                                  sampleLoadEntries[i].name.c_str(),
                                  samplePath)) {
      Trace::Error("SAMPLEPOOL: Failed to resolve sample path for %s",
                   sampleLoadEntries[i].name.c_str());
      continue;
    }

    updateStatus(importIndex, importCount, "Loading");
    if (!loadSample(samplePath.c_str())) {
      Trace::Error("SAMPLEPOOL: Failed to load sample: %s", samplePath.c_str());
      continue;
    }

    const int32_t loadedIndex = count_ - 1;
    if (loadedIndex >= 0) {
      sampleLoadEntries[i].name.copy(nameStore_[loadedIndex],
                                     sampleLoadEntries[i].name.size());
      nameStore_[loadedIndex][sampleLoadEntries[i].name.size()] = '\0';
    }

    if (count_ == MAX_SAMPLES) {
      Trace::Error("Warning maximum sample count reached");
      break;
    }
  }

  // now sort the samples
  int rest = count_;
  while (rest > 0) {
    int index = 0;
    for (int i = 1; i < rest; i++) {
      if (strcmp(names_[i], names_[index]) > 0) {
        index = i;
      };
    };
    swapEntries(index, rest - 1);
    rest--;
  };
};

SoundSource *SamplePool::GetSource(uint32_t i) {
  if (i < 0 || i >= count_) {
    return nullptr;
  }
  return &wav_[i];
};

char **SamplePool::GetNameList() { return names_; };

int SamplePool::GetNameListSize() { return count_; };

uint32_t SamplePool::FindSampleIndexByName(
    const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &name) {
  char **names = GetNameList();
  uint16_t count = GetNameListSize();
  for (uint16_t i = 0; i < count; ++i) {
    // pool entries mirror files in the project samples dir, so match them
    // using the case insensitive semantics of the FAT filesystem
    if (names[i] && strcasecmp(names[i], name.c_str()) == 0) {
      return i;
    }
  }
  return -1;
}

bool SamplePool::RenameLoadedSample(
    const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &oldName,
    const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &newName) {
  const int32_t sampleIndex =
      static_cast<int32_t>(FindSampleIndexByName(oldName));
  if (sampleIndex < 0 || newName.empty() ||
      newName.size() > MAX_INSTRUMENT_FILENAME_LENGTH) {
    return false;
  }

  newName.copy(nameStore_[sampleIndex], newName.size());
  nameStore_[sampleIndex][newName.size()] = '\0';

  SetChanged();
  SamplePoolEvent ev;
  ev.index_ = sampleIndex;
  ev.type_ = SPET_RENAME;
  NotifyObservers(&ev);
  return true;
}

#define IMPORT_CHUNK_SIZE 512
static constexpr int32_t kImportInputSamples =
    IMPORT_CHUNK_SIZE / static_cast<int32_t>(sizeof(int16_t));
// TODO (democloid): This output capacity comes from SRC_MAX_RATIO in the
// bundled sample-rate converter. Separate the up/down limits or move these
// import-only buffers to another memory region so high-rate downsampling does
// not consume unnecessary main RAM.
static constexpr int32_t kImportMaxOutputSamples =
    (kImportInputSamples * SRC_MAX_RATIO) + 8;
static float importResampleIn_[kImportInputSamples];
static float importResampleOut_[kImportMaxOutputSamples];
static int16_t importResampleOutInt16_[kImportMaxOutputSamples];

bool SamplePool::GetProjectSampleFilename(
    const char *sourcePath,
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &projectSampleFilename) {
  if (!extractSampleFilename(sourcePath, projectSampleFilename)) {
    return false;
  }

  // will truncate too long filenames to make sure the filename imported into
  // the project is with filename length limit
  if (projectSampleFilename.is_truncated()) {
    // Truncate the string in-place and then append the extension
    projectSampleFilename =
        projectSampleFilename.substr(0, MAX_INSTRUMENT_FILENAME_LENGTH - 4);
    projectSampleFilename.append(".wav");
  }
  return true;
}

bool SamplePool::CalculateImportSampleSize(uint32_t sourceFrameCount,
                                           uint32_t sourceSampleRate,
                                           uint16_t channelCount,
                                           uint32_t &sampleSize) {
  if (sourceFrameCount == 0 || sourceSampleRate == 0 || channelCount == 0) {
    return false;
  }

  // Round up so the preflight check never underestimates the number of frames
  // that the streaming resampler may produce.
  const uint64_t outputFrameCount =
      (static_cast<uint64_t>(sourceFrameCount) * kImportOutputSampleRate +
       sourceSampleRate - 1U) /
      sourceSampleRate;
  const uint64_t outputBytesPerFrame =
      static_cast<uint64_t>(channelCount) * kImportOutputBytesPerSample;
  if (outputFrameCount > UINT32_MAX / outputBytesPerFrame) {
    return false;
  }

  sampleSize = static_cast<uint32_t>(outputFrameCount * outputBytesPerFrame);
  return true;
}

int SamplePool::ImportSample(const char *name, const char *projectName) {
  return importSample(name, projectName, nullptr);
}

bool SamplePool::ConvertSampleFile(const char *name, const char *outputPath,
                                   SampleConversionErrorCode &errorCode) {
  errorCode = SampleConversionErrorCode::None;
  return importSample(name, nullptr, outputPath, &errorCode) >= 0;
}

int SamplePool::importSample(const char *name, const char *projectName,
                             const char *outputPath,
                             SampleConversionErrorCode *errorCode) {
  const auto fail = [errorCode](SampleConversionErrorCode code) {
    if (errorCode != nullptr) {
      *errorCode = code;
    }
    return -1;
  };
  if (outputPath == nullptr && count_ == MAX_SAMPLES) {
    return -1;
  }

  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> projSampleFilename;
  if (outputPath == nullptr &&
      !GetProjectSampleFilename(name, projSampleFilename)) {
    Trace::Error("Failed to extract sample filename from:%s",
                 name ? name : "(null)");
    return fail(SampleConversionErrorCode::InvalidSourcePath);
  }

  // an existing pool entry with this name is backed by the very file this
  // import would truncate, so refuse rather than clobber it and end up with two
  // entries sharing a single file
  if (outputPath == nullptr &&
      FindSampleIndexByName(projSampleFilename) != static_cast<uint32_t>(-1)) {
    Trace::Error("Sample already in project pool:%s",
                 projSampleFilename.c_str());
    return ImportDuplicateNameError;
  }

  WavFile wav;
  auto wavRes = wav.Open(name);
  if (!wavRes) {
    Trace::Error("Failed to open sample input file:%s", name);
    return fail(SampleConversionErrorCode::InvalidSourceWav);
  }

  const int32_t sourceFrameCount = wav.GetSize(-1);
  const int32_t sourceSampleRate = wav.GetSampleRate(-1);
  const int32_t sourceChannelCount = wav.GetChannelCount(-1);
  uint32_t convertedSampleSize = 0;
  if (sourceFrameCount <= 0 || sourceSampleRate <= 0 ||
      sourceChannelCount <= 0 ||
      !CalculateImportSampleSize(static_cast<uint32_t>(sourceFrameCount),
                                 static_cast<uint32_t>(sourceSampleRate),
                                 static_cast<uint16_t>(sourceChannelCount),
                                 convertedSampleSize)) {
    Trace::Error("Failed to calculate converted sample size for:%s", name);
    return fail(SampleConversionErrorCode::InvalidSourceWav);
  }
  if (outputPath == nullptr && !CheckSampleFits(convertedSampleSize)) {
    Trace::Error("Converted sample doesn't fit in sample storage (need: %u)",
                 convertedSampleSize);
    return -1;
  }

  FileSystemPath::PathString projectSamplePath;
  if (outputPath != nullptr) {
    if (!FileSystemPath::NormalizePath(outputPath, projectSamplePath)) {
      Trace::Error("Invalid converted sample destination:%s", outputPath);
      return fail(SampleConversionErrorCode::InvalidOutputPath);
    }
  } else {
    etl::string<MAX_PROJECT_SAMPLE_PATH_LENGTH> samplePath("/projects/");
    samplePath.append(projectName);
    samplePath.append("/samples/");
    samplePath.append(projSampleFilename);
    projectSamplePath = samplePath.c_str();
  }
  if (outputPath == nullptr) {
    Status::SetMultiLine("Loading %s->\n%s", name, projSampleFilename.c_str());
  } else {
    Status::SetMultiLine("Converting instrument sample");
  }

  auto fout = FileSystem::GetInstance()->Open(projectSamplePath.c_str(), "w");
  if (!fout) {
    Trace::Error("Failed to open sample project file:%s",
                 projectSamplePath.c_str());
    return fail(SampleConversionErrorCode::OutputFileOpenFailed);
  };

  const int32_t importResampler =
      Config::GetInstance()->GetValue("IMPORTRESAMP");
  const bool shouldResample = sourceSampleRate != 44100;
  const int32_t outputSampleRate = kImportOutputSampleRate;

  if (!WavHeaderWriter::WriteHeader(fout.get(), outputSampleRate,
                                    sourceChannelCount, 16)) {
    Trace::Error("Failed to write WAV header for:%s",
                 projectSamplePath.c_str());
    return fail(SampleConversionErrorCode::OutputHeaderWriteFailed);
  }

  // Phase 1: slow filesystem work — copy the import into the project pool
  // before exposing it to the live sample list.
  uint8_t buffer[IMPORT_CHUNK_SIZE];
  uint32_t bytesRead = 0;
  uint32_t samplesRead = 0;
  uint32_t totalRead = 0;
  uint32_t totalWrittenFrames = 0;
  uint32_t totalSize = wav.GetDiskSize(-1);

  importName = name;

  wav.Rewind();
  SRC_STATE *resampler = nullptr;
  if (shouldResample) {
    int32_t converterType = SRC_LINEAR;
    if (importResampler == 2) {
      converterType = SRC_SINC_FASTEST;
    } else if (importResampler == 3) {
      converterType = SRC_SINC_MEDIUM_QUALITY;
    }

    int srcError = SRC_ERR_NO_ERROR;
    resampler =
        createImportResampler(converterType, sourceChannelCount, &srcError);
    if (!resampler) {
      Trace::Error("Failed to initialize resampler (%d)", srcError);
      return fail(SampleConversionErrorCode::ResamplerInitFailed);
    }
    src_reset(resampler);
  }

  const double srcRatio = shouldResample
                              ? (static_cast<double>(kImportOutputSampleRate) /
                                 static_cast<double>(sourceSampleRate))
                              : 1.0;

  while (true) {
    if (!shouldResample) {
      if (!wav.Read(buffer, sizeof(buffer), &bytesRead)) {
        Trace::Error("Failed reading sample data from:%s", name);
        return fail(SampleConversionErrorCode::SourceReadFailed);
      }
      if (bytesRead == 0) {
        break;
      }
      totalRead += bytesRead;
      const uint32_t framesRead =
          bytesRead / (static_cast<uint32_t>(sourceChannelCount) * 2);
      uint32_t written = fout->Write(buffer, 1, bytesRead);
      if (written != bytesRead) {
        Trace::Error("Failed writing sample data to:%s",
                     projectSamplePath.c_str());
        return fail(SampleConversionErrorCode::OutputDataWriteFailed);
      }
      totalWrittenFrames += framesRead;
    } else {
      if (!wav.ReadFloat(importResampleIn_, kImportInputSamples,
                         &samplesRead)) {
        Trace::Error("Failed reading sample data from:%s", name);
        return fail(SampleConversionErrorCode::SourceReadFailed);
      }
      if (samplesRead == 0) {
        break;
      }
      totalRead += samplesRead * 2;

      const uint32_t inputFrames =
          samplesRead / static_cast<uint32_t>(sourceChannelCount);
      uint32_t framesRemaining = inputFrames;
      float *inPtr = importResampleIn_;
      while (framesRemaining > 0) {
        SRC_DATA data;
        memset(&data, 0, sizeof(data));
        data.data_in = inPtr;
        data.input_frames = static_cast<long>(framesRemaining);
        data.data_out = importResampleOut_;
        data.output_frames = static_cast<long>(
            kImportMaxOutputSamples / static_cast<int32_t>(sourceChannelCount));
        data.src_ratio = srcRatio;
        data.end_of_input = 0;

        int err = src_process(resampler, &data);
        if (err != SRC_ERR_NO_ERROR) {
          Trace::Error("Resample failed: %s", src_strerror(err));
          return fail(SampleConversionErrorCode::ResamplingFailed);
        }

        if (data.output_frames_gen > 0) {
          const int32_t outputSamples =
              static_cast<int32_t>(data.output_frames_gen) * sourceChannelCount;
          src_float_to_short_array(importResampleOut_, importResampleOutInt16_,
                                   outputSamples);
          const int32_t bytesToWrite = outputSamples * sizeof(int16_t);
          uint32_t written =
              fout->Write(importResampleOutInt16_, 1, bytesToWrite);
          if (written != static_cast<uint32_t>(bytesToWrite)) {
            Trace::Error("Failed writing sample data to:%s",
                         projectSamplePath.c_str());
            return fail(SampleConversionErrorCode::OutputDataWriteFailed);
          }
          totalWrittenFrames += data.output_frames_gen;
        }

        framesRemaining -= data.input_frames_used;
        inPtr += data.input_frames_used * sourceChannelCount;
      }
    }

    uint32_t total = totalSize;

    importCount = total;
    importIndex = totalRead;
    updateStatus(totalRead, total, "Copying");
  }

  // Flush the resampler to write any delayed tail samples after input ends.
  if (shouldResample && resampler) {
    while (true) {
      SRC_DATA data;
      memset(&data, 0, sizeof(data));
      data.data_in = nullptr;
      data.input_frames = 0;
      data.data_out = importResampleOut_;
      data.output_frames = static_cast<long>(
          kImportMaxOutputSamples / static_cast<int32_t>(sourceChannelCount));
      data.src_ratio = srcRatio;
      data.end_of_input = 1;

      int err = src_process(resampler, &data);
      if (err != SRC_ERR_NO_ERROR) {
        Trace::Error("Resample flush failed: %s", src_strerror(err));
        return fail(SampleConversionErrorCode::ResamplingFailed);
      }

      if (data.output_frames_gen <= 0) {
        break;
      }

      const int32_t outputSamples =
          static_cast<int32_t>(data.output_frames_gen) * sourceChannelCount;
      src_float_to_short_array(importResampleOut_, importResampleOutInt16_,
                               outputSamples);
      const int32_t bytesToWrite = outputSamples * sizeof(int16_t);
      uint32_t written = fout->Write(importResampleOutInt16_, 1, bytesToWrite);
      if (written != static_cast<uint32_t>(bytesToWrite)) {
        Trace::Error("Failed writing sample data to:%s",
                     projectSamplePath.c_str());
        return fail(SampleConversionErrorCode::OutputDataWriteFailed);
      }
      totalWrittenFrames += data.output_frames_gen;
    }
    src_delete(resampler);
  }

  if (!WavHeaderWriter::UpdateFileSize(
          fout.get(),
          shouldResample ? totalWrittenFrames
                         : static_cast<uint32_t>(wav.GetSize(-1)),
          sourceChannelCount, 2)) {
    Trace::Error("Failed to update WAV header for:%s",
                 projectSamplePath.c_str());
    return fail(SampleConversionErrorCode::OutputHeaderFinalizeFailed);
  }

  // Close the output file before re-opening it for import.
  fout.reset();

  if (outputPath != nullptr) {
    return 0;
  }

  // Phase 2: load the project copy into SDRAM. advSamplePool::loadSample()
  // only increments count_ after the backing data is ready, so the new
  // sample remains invisible to the audio engine until the load completes.
  bool status = loadSample(projectSamplePath.c_str());
  if (status) {
    // Replace stored name with truncated filename so matches the potentially
    // truncated filename we actually stored into the project pool subdir
    const int loadedIndex = count_ - 1;
    if (loadedIndex >= 0) {
      projSampleFilename.copy(nameStore_[loadedIndex],
                              projSampleFilename.size());
      nameStore_[loadedIndex][projSampleFilename.size()] = '\0';
    }
  }

  SetChanged();
  SamplePoolEvent ev;
  ev.index_ = count_ - 1;
  ev.type_ = SPET_INSERT;
  NotifyObservers(&ev);
  return status ? (count_ - 1) : -1;
};

// Selection resampling renders directly into the project sample directory;
// this provides the means to load the resample file from the projects sample
// dir into ram and register it in nameStore_ & finally notify observers
int SamplePool::LoadProjectSample(const char *path) {

  if (count_ == MAX_SAMPLES) {
    return -1;
  }

  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> storedName;
  if (!extractSampleFilename(path, storedName)) {
    Trace::Error("Failed to extract sample filename from:%s",
                 path ? path : "(null)");
    return -1;
  }

  if (!loadSample(path)) {
    Trace::Error("Failed to load project sample:%s", path ? path : "(null)");
    return -1;
  }

  const int loadedIndex = count_ - 1;
  if (loadedIndex < 0) {
    return -1;
  }

  storedName.copy(nameStore_[loadedIndex], storedName.size());
  nameStore_[loadedIndex][storedName.size()] = '\0';

  SetChanged();
  SamplePoolEvent ev;
  ev.index_ = loadedIndex;
  ev.type_ = SPET_INSERT;
  NotifyObservers(&ev);
  return loadedIndex;
}

void SamplePool::PurgeSample(int i, const char *projectName) {
  auto fs = FileSystem::GetInstance();

  etl::string<MAX_PROJECT_SAMPLE_PATH_LENGTH> buffer;
  etl::string_stream delPath(buffer);

  delPath << "/" << PROJECTS_DIR << "/" << projectName << "/"
          << PROJECT_SAMPLES_DIR << "/" << names_[i];

  // delete file
  FileSystem::GetInstance()->DeleteFile(delPath.str().c_str());
  // shift all entries from deleted to end
  for (uint32_t j = i; j < count_ - 1; j++) {
    wav_[j] = std::move(wav_[j + 1]);
    memcpy(nameStore_[j], nameStore_[j + 1],
           MAX_INSTRUMENT_FILENAME_LENGTH + 1);
  };
  // decrease sample count
  count_--;
  wav_[count_].Close();
  nameStore_[count_][0] = '\0';

  // now notify observers
  SetChanged();
  SamplePoolEvent ev;
  ev.index_ = i;
  ev.type_ = SPET_DELETE;
  NotifyObservers(&ev);
};

// returns the new samples index or -1 on error
int32_t SamplePool::ReloadSample(uint8_t index, const char *name) {
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> storedName;
  if (!extractSampleFilename(name, storedName)) {
    Trace::Error("Failed to extract sample filename from:%s",
                 name ? name : "(null)");
    return -1;
  }

  if (unloadSample(index) && loadSample(name)) {
    const int32_t loadedIndex = count_ - 1;
    if (loadedIndex >= 0) {
      storedName.copy(nameStore_[loadedIndex], storedName.size());
      nameStore_[loadedIndex][storedName.size()] = '\0';

      SetChanged();
      SamplePoolEvent event;
      event.index_ = loadedIndex;
      event.type_ = SPET_INSERT;
      NotifyObservers(&event);
    }
    return loadedIndex;
  }
  return -1;
}

void SamplePool::swapEntries(int src, int dst) {
  if (src == dst) {
    return;
  }
  std::swap(wav_[src], wav_[dst]);
  char tmp[MAX_INSTRUMENT_FILENAME_LENGTH + 1];
  memcpy(tmp, nameStore_[src], sizeof(tmp));
  memcpy(nameStore_[src], nameStore_[dst], sizeof(tmp));
  memcpy(nameStore_[dst], tmp, sizeof(tmp));
}
