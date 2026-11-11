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

#include "WavHeader.h"
#include "Externals/SRC/common.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/I_File.h"
#include <cstring>

namespace {

constexpr uint16_t WAV_FORMAT_PCM = 0x0001;
constexpr uint16_t WAV_FORMAT_IEEE_FLOAT = 0x0003;
constexpr uint16_t WAV_FORMAT_EXTENSIBLE = 0xFFFE;
constexpr uint8_t WAV_SUBFORMAT_PCM[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x10, 0x00, 0x80, 0x00, 0x00, 0xAA,
                                           0x00, 0x38, 0x9B, 0x71};
constexpr uint8_t WAV_SUBFORMAT_IEEE_FLOAT[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

bool UpdateFileSizeAtOffsets(I_File *file, uint32_t sampleCount,
                             uint16_t channels, uint16_t bytesPerSample,
                             uint32_t dataChunkSizeOffset) {
  if (!file) {
    return false;
  }

  const uint32_t totalFileSize = file->Tell();
  if (totalFileSize < 8) {
    Trace::Error("WAVHEADER: file too small to patch header (%u bytes)",
                 totalFileSize);
    return false;
  }
  if (dataChunkSizeOffset > totalFileSize ||
      (totalFileSize - dataChunkSizeOffset) < sizeof(uint32_t)) {
    Trace::Error("WAVHEADER: invalid data chunk size offset %u (file size %u)",
                 dataChunkSizeOffset, totalFileSize);
    return false;
  }

  const uint32_t chunkSize = totalFileSize - 8;
  const uint32_t dataChunkSize = sampleCount * channels * bytesPerSample;

  file->Seek(4, SEEK_SET);
  int written = file->Write(&chunkSize, 1, 4);
  if (written != 4) {
    Trace::Error("WAVHEADER: failed to write RIFF chunk size (wrote=%d err=%d)",
                 written, file->Error());
    return false;
  }

  Trace::Log("WAVHEADER",
             "Updating header: FileSize=%u, DataSize=%u, DataSizeOffset=%u",
             chunkSize, dataChunkSize, dataChunkSizeOffset);

  file->Seek(dataChunkSizeOffset, SEEK_SET);
  written = file->Write(&dataChunkSize, 1, 4);
  if (written != 4) {
    Trace::Error("WAVHEADER: failed to write data chunk size (wrote=%d err=%d)",
                 written, file->Error());
    return false;
  }

  file->Seek(totalFileSize, SEEK_SET);
  return file->Sync();
}

} // namespace

bool WavHeaderWriter::WriteHeader(I_File *file, uint32_t sampleRate,
                                  uint16_t channels, uint16_t bitsPerSample) {
  if (!file)
    return false;

  // RIFF chunk
  uint32_t chunk = 0x46464952; // "RIFF"
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  uint32_t size = 0; // Placeholder, to be filled later
  if (file->Write(&size, 1, 4) != 4)
    return false;

  // WAVE chunk
  chunk = 0x45564157; // "WAVE"
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  // "fmt " subchunk
  chunk = 0x20746D66; // "fmt "
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  size = 16; // fmt chunk size for PCM
  if (file->Write(&size, 1, 4) != 4)
    return false;

  uint16_t ushort = 1; // PCM compression
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  ushort = channels; // number of channels
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  if (file->Write(&sampleRate, 1, 4) != 4)
    return false;

  uint32_t byteRate = (bitsPerSample / 8) * channels * sampleRate;
  if (file->Write(&byteRate, 1, 4) != 4)
    return false;

  ushort = (bitsPerSample / 8) * channels; // block align
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  ushort = bitsPerSample; // bits per sample
  if (file->Write(&ushort, 1, 2) != 2)
    return false;

  // data subchunk
  chunk = 0x61746164; // "data"
  if (file->Write(&chunk, 1, 4) != 4)
    return false;

  size = 0; // Placeholder, to be updated later
  if (file->Write(&size, 1, 4) != 4)
    return false;

  return file->Sync();
}

etl::expected<WavHeaderInfo, WAVEFILE_ERROR>
WavHeaderWriter::ReadHeader(I_File *file) {
  if (!file) {
    return etl::unexpected(INVALID_FILE);
  }

  file->Seek(0, SEEK_SET);

  WavHeaderInfo info;

  uint32_t chunk = 0;
  if (file->Read(&chunk, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (chunk != 0x46464952) { // "RIFF"
    Trace::Error("WavHeaderWriter: Missing RIFF identifier");
    return etl::unexpected(UNSUPPORTED_FILE_FORMAT);
  }

  if (file->Read(&info.riffChunkSize, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&chunk, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (chunk != 0x45564157) { // "WAVE"
    Trace::Error("WavHeaderWriter: Missing WAVE identifier");
    return etl::unexpected(UNSUPPORTED_WAV_FORMAT);
  }

  const long afterWavePos = file->Tell();
  file->Seek(0, SEEK_END);
  const uint32_t fileEnd = static_cast<uint32_t>(file->Tell());
  file->Seek(afterWavePos, SEEK_SET);
  if (fileEnd <= 0) {
    return etl::unexpected(INVALID_HEADER);
  }

  const uint32_t riffEnd = info.riffChunkSize + 8;
  bool fmtFound = false;

  while (!fmtFound) {
    if (file->Read(&chunk, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }

    uint32_t chunkSize = 0;
    if (file->Read(&chunkSize, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }
    info.fmtChunkSize = chunkSize;

    uint32_t chunkDataOffset = file->Tell();
    uint32_t paddedChunkSize = info.fmtChunkSize + (info.fmtChunkSize & 1);
    uint32_t nextOffset = chunkDataOffset + paddedChunkSize;
    if (nextOffset > riffEnd) {
      Trace::Error("WavHeaderWriter: fmt chunk exceeds RIFF bounds");
      return etl::unexpected(INVALID_HEADER);
    }

    if (chunk == 0x20746D66) { // "fmt "
      fmtFound = true;
      break;
    }

    file->Seek(info.fmtChunkSize, SEEK_CUR);

    if (info.fmtChunkSize & 1) {
      file->Seek(1, SEEK_CUR);
    }
  }

  if (!fmtFound) {
    Trace::Error("WavHeaderWriter: fmt chunk missing");
    return etl::unexpected(INVALID_HEADER);
  }

  if (info.fmtChunkSize < 16) {
    Trace::Error("WavHeaderWriter: fmt chunk too small");
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.audioFormat, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  const bool isExtensible = info.audioFormat == WAV_FORMAT_EXTENSIBLE;
  if (info.audioFormat != WAV_FORMAT_PCM &&
      info.audioFormat != WAV_FORMAT_IEEE_FLOAT && !isExtensible) {
    Trace::Error("WavHeaderWriter: Unsupported audio format %u",
                 info.audioFormat);
    return etl::unexpected(UNSUPPORTED_AUDIO_FORMAT);
  }

  if (file->Read(&info.numChannels, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (info.numChannels == 0) {
    Trace::Error("WavHeaderWriter: Invalid channel count 0");
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.sampleRate, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if ((info.sampleRate < 44100 / SRC_MAX_RATIO) ||
      (info.sampleRate > 44100 * SRC_MAX_RATIO)) {
    Trace::Error("WavHeaderWriter: Unsupported sample rate %u",
                 info.sampleRate);
    return etl::unexpected(UNSUPPORTED_SAMPLERATE);
  }

  if (file->Read(&info.byteRate, 4) != 4) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.blockAlign, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (file->Read(&info.bitsPerSample, 2) != 2) {
    return etl::unexpected(INVALID_HEADER);
  }

  if (isExtensible) {
    if (info.fmtChunkSize < 40) {
      Trace::Error("WavHeaderWriter: Extensible fmt chunk too small (%u)",
                   info.fmtChunkSize);
      return etl::unexpected(INVALID_HEADER);
    }

    uint16_t extensionSize = 0;
    uint16_t validBitsPerSample = 0;
    uint32_t channelMask = 0;
    uint8_t subFormat[16] = {0};

    if (file->Read(&extensionSize, 2) != 2 ||
        file->Read(&validBitsPerSample, 2) != 2 ||
        file->Read(&channelMask, 4) != 4 || file->Read(subFormat, 16) != 16) {
      return etl::unexpected(INVALID_HEADER);
    }

    (void)extensionSize;
    (void)validBitsPerSample;
    (void)channelMask;

    if (std::memcmp(subFormat, WAV_SUBFORMAT_PCM, sizeof(subFormat)) == 0) {
      info.audioFormat = WAV_FORMAT_PCM;
    } else if (std::memcmp(subFormat, WAV_SUBFORMAT_IEEE_FLOAT,
                           sizeof(subFormat)) == 0) {
      info.audioFormat = WAV_FORMAT_IEEE_FLOAT;
    } else {
      Trace::Error("WavHeaderWriter: Unsupported extensible subformat GUID");
      return etl::unexpected(UNSUPPORTED_AUDIO_FORMAT);
    }
  }

  const bool isPcm = info.audioFormat == WAV_FORMAT_PCM;
  const bool isFloat = info.audioFormat == WAV_FORMAT_IEEE_FLOAT;

  if (isPcm) {
    if ((info.bitsPerSample != 8) && (info.bitsPerSample != 16) &&
        (info.bitsPerSample != 24) && (info.bitsPerSample != 32)) {
      Trace::Error("WavHeaderWriter: Unsupported PCM bit depth %u",
                   info.bitsPerSample);
      return etl::unexpected(UNSUPPORTED_BITDEPTH);
    }
  } else if (isFloat) {
    if ((info.bitsPerSample != 32) && (info.bitsPerSample != 64)) {
      Trace::Error("WavHeaderWriter: Unsupported IEEE float bit depth %u",
                   info.bitsPerSample);
      return etl::unexpected(UNSUPPORTED_BITDEPTH);
    }
  }

  info.bytesPerSample = info.bitsPerSample / 8;

  const uint32_t fmtBytesConsumed = isExtensible ? 40U : 16U;
  if (info.fmtChunkSize > fmtBytesConsumed) {
    uint32_t toSkip = info.fmtChunkSize - fmtBytesConsumed;
    if (file->Tell() + toSkip > riffEnd) {
      Trace::Error("WavHeaderWriter: fmt extra data exceeds RIFF bounds");
      return etl::unexpected(INVALID_HEADER);
    }
    file->Seek(toSkip, SEEK_CUR);
  }
  if (info.fmtChunkSize & 1) {
    file->Seek(1, SEEK_CUR);
  }

  while (true) {
    if (file->Read(&chunk, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }

    uint32_t chunkSize = 0;
    if (file->Read(&chunkSize, 4) != 4) {
      return etl::unexpected(INVALID_HEADER);
    }

    uint32_t dataStart = file->Tell();
    uint32_t paddedChunkSize = chunkSize + (chunkSize & 1);
    uint32_t chunkEnd = dataStart + paddedChunkSize;
    if (chunkEnd > riffEnd) {
      // Some exporters write a too-small RIFF size while keeping a valid data
      // chunk that ends at/before EOF. Accept only this narrow mismatch.
      if (chunk == 0x61746164 && chunkEnd <= fileEnd) { // "data"
        Trace::Log("WAVHEADER",
                   "Accepting data chunk beyond RIFF bounds (riffEnd=%u, "
                   "chunkEnd=%u, fileEnd=%u)",
                   riffEnd, chunkEnd, fileEnd);
      } else {
        Trace::Error("WavHeaderWriter: data chunk exceeds RIFF bounds");
        return etl::unexpected(INVALID_HEADER);
      }
    }

    if (chunk == 0x61746164) { // "data"
      info.dataChunkSize = chunkSize;
      info.dataChunkSizeOffset = dataStart - sizeof(uint32_t);
      info.dataOffset = dataStart;
      break;
    }

    file->Seek(chunkSize, SEEK_CUR);

    if (chunkSize & 1) {
      file->Seek(1, SEEK_CUR);
    }

    if (static_cast<uint32_t>(file->Tell()) >= riffEnd) {
      Trace::Error("WavHeaderWriter: data chunk not found within RIFF bounds");
      return etl::unexpected(INVALID_HEADER);
    }
  }

  if (info.dataChunkSize == 0) {
    Trace::Error("WavHeaderWriter: Missing or empty data chunk");
    return etl::unexpected(INVALID_HEADER);
  }

  file->Seek(info.dataOffset, SEEK_SET);
  return info;
}
bool WavHeaderWriter::UpdateFileSize(I_File *file, uint32_t sampleCount,
                                     uint16_t channels,
                                     uint16_t bytesPerSample) {
  return UpdateFileSizeAtOffsets(file, sampleCount, channels, bytesPerSample,
                                 40);
}

bool WavHeaderWriter::UpdateFileSize(I_File *file, uint32_t sampleCount,
                                     const WavHeaderInfo &headerInfo) {
  if (headerInfo.numChannels == 0 || headerInfo.bytesPerSample == 0) {
    Trace::Error(
        "WAVHEADER: invalid channel or sample width in parsed header (%u, %u)",
        headerInfo.numChannels, headerInfo.bytesPerSample);
    return false;
  }

  return UpdateFileSizeAtOffsets(file, sampleCount, headerInfo.numChannels,
                                 headerInfo.bytesPerSample,
                                 headerInfo.dataChunkSizeOffset);
}
