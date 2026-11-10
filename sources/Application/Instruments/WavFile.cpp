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

#include "WavFile.h"
#include "Application/Model/Config.h"
#include "Application/Model/Song.h"
#include "Foundation/Types/Types.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/I_File.h"
#include "WavHeader.h"
#include "config/MemorySections.h"
#include <cmath>
#include <limits>
#include <stdlib.h>

unsigned char WavFile::readBuffer_[BUFFER_SIZE];
int16_t WavFile::convertedBuffer_[BUFFER_SIZE / 2];

int16_t ClampToInt16(double sample) {
  float s = static_cast<float>(sample);
  if (s <= -1.0f)
    return -32768;
  if (s >= +1.0f)
    return +32767;
  return static_cast<int16_t>(s * 32768.0f);
}

int16_t ConvertSampleToInt16(const uint8_t *samplePtr, uint16_t audioFormat,
                             int32_t bytePerSample) {

  if (audioFormat == 1) { // PCM
    switch (bytePerSample) {
    case 1: {
      // expand 8-bit to 16-bit
      // 8-bit PCM is unsigned while >8-bit is signed
      return static_cast<int16_t>((static_cast<int16_t>(samplePtr[0]) - 128)
                                  << 8);
    }
    case 2: {
      // signed 16-bit
      int16_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return value;
    }
    case 3: {
      // signed 24-bit
      int32_t value = samplePtr[0] | (samplePtr[1] << 8) |
                      (static_cast<int32_t>(samplePtr[2]) << 16);
      value = (value << 8) >> 8; // Sign extend
      return static_cast<int16_t>(value >> 8);
    }
    case 4: {
      // signed 32-bit
      int32_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return static_cast<int16_t>(value >> 16);
    }
    default:
      break;
    }
  } else if (audioFormat == 3) { // IEEE float
    if (bytePerSample == 4) {
      float value;
      memcpy(&value, samplePtr, sizeof(value));
      return ClampToInt16(static_cast<double>(value));
    }
    if (bytePerSample == 8) {
      double value;
      memcpy(&value, samplePtr, sizeof(value));
      return ClampToInt16(value);
    }
  }

  Trace::Error("WAVFILE: Unsupported format (%u) or byte depth (%d)",
               audioFormat, bytePerSample);
  return 0;
}

float ConvertSampleToFloat(const uint8_t *samplePtr, uint16_t audioFormat,
                           int32_t bytePerSample) {
  if (audioFormat == 1) { // PCM
    switch (bytePerSample) {
    case 1: {
      int16_t v = static_cast<int16_t>(samplePtr[0]) - 128;
      return static_cast<float>(v) / 128.0f;
    }
    case 2: {
      int16_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return static_cast<float>(value) / 32768.0f;
    }
    case 3: {
      int32_t value = samplePtr[0] | (samplePtr[1] << 8) |
                      (static_cast<int32_t>(samplePtr[2]) << 16);
      value = (value << 8) >> 8; // Sign extend
      return static_cast<float>(value) / 8388608.0f;
    }
    case 4: {
      int32_t value;
      memcpy(&value, samplePtr, sizeof(value));
      return static_cast<float>(value) / 2147483648.0f;
    }
    default:
      break;
    }
  } else if (audioFormat == 3) { // IEEE float
    if (bytePerSample == 4) {
      float value;
      memcpy(&value, samplePtr, sizeof(value));
      return value;
    }
    if (bytePerSample == 8) {
      double value;
      memcpy(&value, samplePtr, sizeof(value));
      return static_cast<float>(value);
    }
  }

  Trace::Error("WAVFILE: Unsupported format (%u) or byte depth (%d)",
               audioFormat, bytePerSample);
  return 0.0f;
}

namespace {
uint32_t readBigEndian(const uint8_t *bytes, uint32_t count) {
  uint32_t value = 0;
  for (uint32_t index = 0; index < count; ++index) {
    value = (value << 8) | bytes[index];
  }
  return value;
}

etl::expected<WavHeaderInfo, WAVEFILE_ERROR> readAiffHeader(I_File *file) {
  file->Seek(0, SEEK_END);
  const int64_t fileSize = file->Tell();
  file->Seek(0, SEEK_SET);
  uint8_t form[12];
  if (file->Read(form, sizeof(form)) != sizeof(form) ||
      std::memcmp(form, "FORM", 4) != 0 ||
      std::memcmp(form + 8, "AIFF", 4) != 0) {
    return etl::unexpected(UNSUPPORTED_FILE_FORMAT);
  }
  const uint64_t formEnd = uint64_t{readBigEndian(form + 4, 4)} + 8;
  if (formEnd < 12 || formEnd > static_cast<uint64_t>(fileSize)) {
    return etl::unexpected(INVALID_HEADER);
  }
  WavHeaderInfo header{};
  uint32_t frames = 0;
  uint32_t soundBytes = 0;
  bool commonFound = false;
  bool soundFound = false;
  uint64_t position = 12;
  while (position + 8 <= formEnd) {
    file->Seek(static_cast<int32_t>(position), SEEK_SET);
    uint8_t chunk[8];
    if (file->Read(chunk, sizeof(chunk)) != sizeof(chunk)) {
      return etl::unexpected(INVALID_HEADER);
    }
    const uint32_t length = readBigEndian(chunk + 4, 4);
    const uint64_t end = position + 8 + length;
    if (end > formEnd || end > INT32_MAX) {
      return etl::unexpected(INVALID_HEADER);
    }
    if (std::memcmp(chunk, "COMM", 4) == 0) {
      uint8_t common[18];
      if (commonFound || length < sizeof(common) ||
          file->Read(common, sizeof(common)) != sizeof(common)) {
        return etl::unexpected(INVALID_HEADER);
      }
      header.numChannels = readBigEndian(common, 2);
      frames = readBigEndian(common + 2, 4);
      header.bitsPerSample = readBigEndian(common + 6, 2);
      if ((header.numChannels != 1 && header.numChannels != 2) ||
          (header.bitsPerSample != 8 && header.bitsPerSample != 16 &&
           header.bitsPerSample != 24 && header.bitsPerSample != 32)) {
        return etl::unexpected(UNSUPPORTED_AUDIO_FORMAT);
      }
      const uint16_t exponent = readBigEndian(common + 8, 2);
      const uint64_t mantissa =
          (uint64_t{readBigEndian(common + 10, 4)} << 32) |
          readBigEndian(common + 14, 4);
      const double rate =
          std::ldexp(static_cast<double>(mantissa),
                     static_cast<int32_t>(exponent) - 16383 - 63);
      if ((exponent & 0x8000) || exponent == 0 || exponent == 0x7fff ||
          !(mantissa & (uint64_t{1} << 63)) || !std::isfinite(rate) ||
          rate < 1 || rate > INT32_MAX || rate != std::floor(rate)) {
        return etl::unexpected(UNSUPPORTED_SAMPLERATE);
      }
      header.sampleRate = static_cast<uint32_t>(rate);
      header.bytesPerSample = header.bitsPerSample / 8;
      header.audioFormat = 1;
      commonFound = true;
    } else if (std::memcmp(chunk, "SSND", 4) == 0) {
      uint8_t sound[8];
      if (soundFound || length < sizeof(sound) ||
          file->Read(sound, sizeof(sound)) != sizeof(sound)) {
        return etl::unexpected(INVALID_HEADER);
      }
      const uint32_t offset = readBigEndian(sound, 4);
      if (offset > length - 8) {
        return etl::unexpected(INVALID_HEADER);
      }
      header.dataOffset = static_cast<uint32_t>(position + 16 + offset);
      soundBytes = length - 8 - offset;
      soundFound = true;
    }
    position = end + (length & 1u);
  }
  const uint64_t dataBytes =
      uint64_t{frames} * header.numChannels * header.bytesPerSample;
  if (!commonFound || !soundFound || frames == 0 || dataBytes > soundBytes ||
      uint64_t{frames} * header.numChannels * 2 > INT32_MAX) {
    return etl::unexpected(INVALID_HEADER);
  }
  header.dataChunkSize = static_cast<uint32_t>(dataBytes);
  return header;
}

// Normalize AIFF signed big-endian PCM for the existing WAV decoders.
const uint8_t *normalizeAiff(const uint8_t *sample, uint8_t *normalized,
                             uint32_t width, bool aiff) {
  if (!aiff) {
    return sample;
  }
  for (uint32_t index = 0; index < width; ++index) {
    normalized[index] = sample[width - index - 1];
  }
  if (width == 1) {
    normalized[0] ^= 0x80;
  }
  return normalized;
}
} // namespace

WavFile::WavFile()
    : file_(), readBufferSize_(0), samples_(nullptr), sampleBufferSize_(0),
      size_(0), sampleRate_(0), channelCount_(0), bytePerSample_(0),
      audioFormat_(0), dataPosition_(0), readCount_(0) {}

etl::expected<void, WAVEFILE_ERROR> WavFile::Open(const char *name) {
  // open file
  FileSystem *fs = FileSystem::GetInstance();
  auto file = fs->Open(name, "r");

  if (!file)
    return etl::unexpected(INVALID_FILE);

  uint8_t magic[4];
  const bool isAiff = file->Read(magic, sizeof(magic)) == sizeof(magic) &&
                      std::memcmp(magic, "FORM", 4) == 0;
  file->Seek(0, SEEK_SET);
  auto header = isAiff ? readAiffHeader(file.get())
                       : WavHeaderWriter::ReadHeader(file.get());
  if (!header) {
    return etl::unexpected(header.error());
  }

  file_ = std::move(file);
  aiff_ = isAiff;

  sampleRate_ = header->sampleRate;
  channelCount_ = header->numChannels;
  bytePerSample_ = header->bytesPerSample;
  audioFormat_ = header->audioFormat;

  Trace::Debug("File data bytes: %u", header->dataChunkSize);

  size_ =
      header->dataChunkSize / (header->numChannels * header->bytesPerSample);
  Trace::Debug("File sample count: %i", size_);

  // All samples are saved as 16bit/sample in memory
  sampleBufferSize_ = size_ * header->numChannels * 2;
  Trace::Debug("File sampleBufferSize_: %i", sampleBufferSize_);

  readCount_ = header->dataChunkSize;
  dataPosition_ = header->dataOffset;

  readBufferSize_ = 0;
  samples_ = nullptr;

  file_->Seek(header->dataOffset, SEEK_SET);
  return {};
};

void *WavFile::GetSampleBuffer(int note) { return samples_; };

void WavFile::SetSampleBuffer(short *ptr) { samples_ = ptr; }

PICOTRACKER_FAST_CODE("wav_file_get_size")
int WavFile::GetSize(int note) { return size_; };

int WavFile::GetChannelCount(int note) { return channelCount_; };

int WavFile::GetSampleRate(int note) { return sampleRate_; };

float WavFile::GetLengthInSec() { return (float)size_ / sampleRate_; };

long WavFile::readBlock(long start, long size) {
  if (size > readBufferSize_) {
    readBufferSize_ = size;
  }
  file_->Seek(start, SEEK_SET);
  file_->Read(readBuffer_, size);
  return size;
};

bool WavFile::GetBuffer(long start, long size) {
  samples_ = convertedBuffer_;

  const int32_t totalSamples = size * channelCount_;
  const int32_t maxSamples =
      static_cast<int32_t>(sizeof(convertedBuffer_) / sizeof(int16_t));
  if (totalSamples > maxSamples) {
    Trace::Error("WAVFILE: Requested buffer too large (%ld frames)", size);
    return false;
  }

  const int32_t bytesPerFrame = channelCount_ * bytePerSample_;
  const int32_t maxFramesPerRead =
      (bytesPerFrame > 0) ? (BUFFER_SIZE / bytesPerFrame) : 0;
  if (maxFramesPerRead == 0) {
    Trace::Error("WAVFILE: Invalid frame sizing");
    return false;
  }

  int32_t bufferStart = dataPosition_ + start * bytesPerFrame;
  int32_t framesRemaining = size;
  int32_t dstOffset = 0;

  while (framesRemaining > 0) {
    const int32_t framesThisRead =
        std::min<int32_t>(framesRemaining, maxFramesPerRead);
    const int32_t readSize = framesThisRead * bytesPerFrame;

    readBlock(bufferStart, readSize);

    for (int32_t i = 0; i < framesThisRead * channelCount_; ++i) {
      uint8_t normalized[4];
      const uint8_t *samplePtr = readBuffer_ + i * bytePerSample_;
      convertedBuffer_[dstOffset + i] = ConvertSampleToInt16(
          normalizeAiff(samplePtr, normalized, bytePerSample_, aiff_),
          audioFormat_, bytePerSample_);
    }
    bufferStart += readSize;
    framesRemaining -= framesThisRead;
    dstOffset += framesThisRead * channelCount_;
  }
  return true;
};

uint32_t WavFile::GetDiskSize(int note) { return sampleBufferSize_; }

// rewind to start of data (no header)
bool WavFile::Rewind() {
  file_->Seek(dataPosition_, SEEK_SET);
  readCount_ = size_ * channelCount_ * bytePerSample_;
  return true;
};

// incrementally read file, use rewind method to go to beginning
bool WavFile::Read(void *buff, uint32_t btr, uint32_t *bytesRead) {

  if (!buff || !bytesRead) {
    return false;
  }

  *bytesRead = 0;

  if (btr == 0 || readCount_ == 0) {
    return true;
  }

  // dst is always 16-bit
  uint32_t dstFrameSize = channelCount_ * 2;
  // src can be 8/16/24-bit
  uint32_t srcFrameSize = channelCount_ * bytePerSample_;
  // the max number of frames we can read (floor)
  uint32_t dstFrames = btr / dstFrameSize;
  // Also cap by how many source bytes fit in caller buffer to avoid overflow
  uint32_t srcFramesByBuffer = (srcFrameSize > 0) ? (btr / srcFrameSize) : 0;
  // the number of frames that still have to be read
  uint32_t srcFrames = readCount_ / srcFrameSize;

  uint32_t framesToRead = std::min({dstFrames, srcFrames, srcFramesByBuffer});
  if (framesToRead == 0) {
    return true;
  }
  uint32_t readSize = framesToRead * srcFrameSize;

  uint32_t actualBytesRead = file_->Read(buff, readSize);
  // We should have enough capacity to read all of readSize, if we don't, we
  // need to adjust the file position to where we actually read, adjusting to a
  // full frame
  uint32_t missing = actualBytesRead % srcFrameSize;
  // if we have reminder bytes to read, rewind to last frame read in preparation
  // for next iteration
  if (missing != 0) {
    file_->Seek(-static_cast<long>(missing), SEEK_CUR);
    actualBytesRead -= missing;
  }

  // If what we actually read was less than a frame we could end up with 0 bytes
  // read. Assume the rest of the file does not contain a full frame (unlikely)
  if (actualBytesRead == 0) {
    return true;
  }

  uint32_t framesRead = actualBytesRead / srcFrameSize;
  uint32_t totalSamples = framesRead * channelCount_;

  // TODO: we repeat this logic in two places
  // Now adjust the samples
  uint8_t *src = static_cast<uint8_t *>(buff);
  int16_t *dst = static_cast<int16_t *>(buff);
  if (bytePerSample_ == 1) {
    // Expanding 8-bit to 16-bit; convert backward to avoid overwrite
    for (int32_t i = static_cast<int32_t>(totalSamples) - 1; i >= 0; --i) {
      uint8_t normalized[4];
      const uint8_t *samplePtr = src + i * bytePerSample_;
      dst[i] = ConvertSampleToInt16(
          normalizeAiff(samplePtr, normalized, bytePerSample_, aiff_),
          audioFormat_, bytePerSample_);
    }
  } else {
    // retain or shrink width
    for (uint32_t i = 0; i < totalSamples; ++i) {
      uint8_t normalized[4];
      const uint8_t *samplePtr = src + i * bytePerSample_;
      dst[i] = ConvertSampleToInt16(
          normalizeAiff(samplePtr, normalized, bytePerSample_, aiff_),
          audioFormat_, bytePerSample_);
    }
  }
  *bytesRead = framesRead * dstFrameSize;

  readCount_ -= actualBytesRead;
  return true;
}

// Resampler takes it's input in float, so we read as float if we are resampling
// so that we don't lose precision from higher bitrate samples
bool WavFile::ReadFloat(float *buff, uint32_t maxSamples,
                        uint32_t *samplesRead) {
  if (!buff || !samplesRead) {
    return false;
  }

  *samplesRead = 0;

  if (maxSamples == 0 || readCount_ == 0) {
    return true;
  }

  const uint32_t srcFrameSize = channelCount_ * bytePerSample_;
  if (srcFrameSize == 0) {
    return false;
  }

  const uint32_t maxFramesByOutput = maxSamples / channelCount_;
  const uint32_t maxFramesByReadCount = readCount_ / srcFrameSize;
  uint32_t framesRemaining = std::min(maxFramesByOutput, maxFramesByReadCount);
  if (framesRemaining == 0) {
    return true;
  }

  const uint32_t maxFramesPerRead = BUFFER_SIZE / srcFrameSize;
  if (maxFramesPerRead == 0) {
    return false;
  }

  uint32_t framesReadTotal = 0;

  while (framesRemaining > 0) {
    const uint32_t framesThisRead = std::min(framesRemaining, maxFramesPerRead);
    const uint32_t readSize = framesThisRead * srcFrameSize;

    uint32_t actualBytesRead = file_->Read(readBuffer_, readSize);
    uint32_t missing = actualBytesRead % srcFrameSize;
    if (missing != 0) {
      file_->Seek(-static_cast<int>(missing), SEEK_CUR);
      actualBytesRead -= missing;
    }
    if (actualBytesRead == 0) {
      break;
    }

    const uint32_t framesRead = actualBytesRead / srcFrameSize;
    const uint32_t totalSamples = framesRead * channelCount_;
    const uint8_t *src = readBuffer_;
    float *dst = buff + (framesReadTotal * channelCount_);

    for (uint32_t i = 0; i < totalSamples; ++i) {
      uint8_t normalized[4];
      const uint8_t *samplePtr = src + i * bytePerSample_;
      dst[i] = ConvertSampleToFloat(
          normalizeAiff(samplePtr, normalized, bytePerSample_, aiff_),
          audioFormat_, bytePerSample_);
    }

    framesReadTotal += framesRead;
    framesRemaining -= framesRead;
    readCount_ -= actualBytesRead;

    if (actualBytesRead < readSize) {
      break;
    }
  }

  *samplesRead = framesReadTotal * channelCount_;
  return true;
}

bool WavFile::IsOpen() const { return static_cast<bool>(file_); }

void WavFile::Close() {
  if (file_) {
    file_.reset();
  }
  readBufferSize_ = 0;
}

int WavFile::GetRootNote(int note) { return NOTE_C3; }
