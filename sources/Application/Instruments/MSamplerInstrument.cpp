/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "MSamplerInstrument.h"

#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleStreamService.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/PathUtils.h"
#include "System/Profiler/StreamingPerf.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kPcmScale = static_cast<float>(kAudioPcmScale);
constexpr uint32_t kArenaAlignment = 32;
constexpr uint8_t kPageEmpty = 0;
constexpr uint8_t kPageLoading = 1;
constexpr uint8_t kPageReady = 2;
constexpr uint8_t kPageFailed = 3;

uint32_t alignArena(uint32_t size) {
  return (size + kArenaAlignment - 1) & ~(kArenaAlignment - 1);
}
} // namespace

MSamplerInstrument::MSamplerInstrument()
    : I_Instrument(&variables_),
      level_(FourCC::SampleSourceInstrumentLevel, 0xC0),
      table_(FourCC::SampleSourceInstrumentTable, -1),
      tableAutomation_(FourCC::SampleSourceInstrumentTableAutomation, false) {
  variables_.push_back(&level_);
  variables_.push_back(&table_);
  variables_.push_back(&tableAutomation_);
  tableState_.Reset();
}

MSamplerInstrument::~MSamplerInstrument() { releaseArena(); }

bool MSamplerInstrument::Init() {
  tableState_.Reset();
  return true;
}

void MSamplerInstrument::OnStart() { tableState_.Reset(); }

bool MSamplerInstrument::IsInitialized() { return preset_ != nullptr; }

bool MSamplerInstrument::IsEmpty() { return preset_ == nullptr; }

bool MSamplerInstrument::LoadPreset(const char *path) {
  if (path == nullptr || path[0] == '\0' ||
      std::strlen(path) >= presetPath_.max_size()) {
    return false;
  }
  if (!SampleStreamService::Start()) {
    Trace::Error("MSAMPLER: failed to start the SD stream worker");
    return false;
  }

  for (auto &channel : channels_) {
    channel.active_ = false;
  }
  SampleStreamService::Wait(pendingReads_);
  releaseArena();

  SamplePool *pool = SamplePool::GetInstance();
  arena_ = static_cast<uint8_t *>(pool->ReserveStreamingStorage(ArenaBytes));
  if (arena_ == nullptr) {
    Trace::Error("MSAMPLER: unable to reserve the 4 MiB stream arena");
    return false;
  }
  std::memset(arena_, 0, ArenaBytes);
  preset_ = reinterpret_cast<DecentSamplerPreset *>(arena_);

  const auto result = LoadDecentSamplerPreset(path, *preset_);
  if (!result) {
    Trace::Error("MSAMPLER: preset load failed at line %u: %s", result.line_,
                 result.message_);
    releaseArena();
    return false;
  }

  attackBase_ = alignArena(sizeof(DecentSamplerPreset));
  streamBase_ = attackBase_ + AttackCacheBytes;
  const uint32_t bytesPerZone =
      (AttackCacheBytes / preset_->zoneCount_) & ~(kArenaAlignment - 1);
  std::atomic<uint8_t> readState{kPageEmpty};

  for (uint16_t index = 0; index < preset_->zoneCount_; ++index) {
    DecentSamplerZone &zone = preset_->zones_[index];
    const uint32_t bytesPerFrame = zone.channelCount_ * sizeof(int16_t);
    const uint32_t availableFrames =
        (zone.endFrame_ == 0 ? zone.frameCount_ : zone.endFrame_) -
        zone.startFrame_;
    const uint32_t attackFrames =
        std::min(availableFrames, bytesPerZone / bytesPerFrame);
    if (attackFrames == 0) {
      Trace::Error("MSAMPLER: attack cache cannot fit zone %u", index);
      releaseArena();
      return false;
    }

    zone.attackOffset_ = attackBase_ + index * bytesPerZone;
    zone.attackFrames_ = attackFrames;
    const uint32_t attackBytes = attackFrames * bytesPerFrame;
    const uint64_t dataPosition =
        static_cast<uint64_t>(zone.dataOffset_) +
        static_cast<uint64_t>(zone.startFrame_) * bytesPerFrame;
    if (dataPosition > UINT32_MAX ||
        !SampleStreamService::Read(zone.path_,
                                   static_cast<uint32_t>(dataPosition),
                                   arena_ + zone.attackOffset_, attackBytes,
                                   readState, pendingReads_)) {
      Trace::Error("MSAMPLER: failed to cache attack for zone %u", index);
      releaseArena();
      return false;
    }
    SampleStreamService::Wait(pendingReads_);
    if (readState.load(std::memory_order_acquire) != kPageReady) {
      Trace::Error("MSAMPLER: failed reading attack data for zone %u", index);
      releaseArena();
      return false;
    }
  }

  presetPath_ = path;
  FileSystemPath::PathString presetFileName;
  if (FileSystemPath::BaseName(path, presetFileName)) {
    char displayName[MAX_INSTRUMENT_NAME_LENGTH + 1]{};
    const size_t nameLength =
        std::min(presetFileName.size(), sizeof(displayName) - 1);
    std::memcpy(displayName, presetFileName.c_str(), nameLength);
    char *dot = std::strrchr(displayName, '.');
    if (dot != nullptr) {
      *dot = '\0';
      SetName(displayName);
    } else {
      SetName(displayName);
    }
  }
  Trace::Log("MSAMPLER", "Loaded %u zones from %s", preset_->zoneCount_, path);
  return true;
}

bool MSamplerInstrument::Start(int channel, unsigned char note,
                               InstrumentStartMode mode) {
  return StartWithVelocity(channel, note, 127, mode);
}

bool MSamplerInstrument::StartWithVelocity(int channel, unsigned char note,
                                           uint8_t velocity,
                                           InstrumentStartMode mode) {
  if (channel < 0 || channel >= static_cast<int>(channels_.size()) ||
      preset_ == nullptr) {
    return false;
  }
  ChannelState &state = channels_[static_cast<size_t>(channel)];
  if (mode == InstrumentStartMode::TiedNote && state.active_ &&
      state.zoneIndex_ < preset_->zoneCount_) {
    state.baseSpeed_ = std::clamp(
        std::pow(2.0f, (static_cast<float>(note) -
                        preset_->zones_[state.zoneIndex_].rootNote_) /
                           12.0f),
        0.25f, 4.0f);
    state.speed_ = state.baseSpeed_;
    return true;
  }

  const uint32_t zoneIndex = findZone(note, velocity);
  if (zoneIndex == InvalidZone) {
    state.active_ = false;
    return false;
  }

  ++state.generation_;
  if (state.generation_ == 0) {
    ++state.generation_;
  }
  state.active_ = true;
  state.starved_ = false;
  state.zoneIndex_ = zoneIndex;
  state.position_ = static_cast<float>(preset_->zones_[zoneIndex].startFrame_);
  state.baseSpeed_ =
      std::clamp(std::pow(2.0f, (static_cast<float>(note) -
                                 preset_->zones_[zoneIndex].rootNote_) /
                                    12.0f),
                 0.25f, 4.0f);
  state.speed_ = state.baseSpeed_;
  state.level_ = level_.GetInt() / 255.0f;
  state.lastLeft_ = 0.0f;
  state.lastRight_ = 0.0f;
  state.releaseSamples_ = 0;
  const DecentSamplerZone &zone = preset_->zones_[zoneIndex];
  state.nextReadFrame_ = zone.startFrame_ + zone.attackFrames_;

  for (uint8_t pageIndex = 0; pageIndex < PagesPerChannel; ++pageIndex) {
    StreamPage &page = state.pages_[pageIndex];
    if (page.state_.load(std::memory_order_acquire) != kPageLoading) {
      page.state_.store(kPageEmpty, std::memory_order_release);
      page.generation_ = state.generation_;
      page.data_ = reinterpret_cast<int16_t *>(
          arena_ + streamBase_ +
          (static_cast<uint32_t>(channel) * PagesPerChannel + pageIndex) *
              StreamPageBytes);
    }
  }
  for (auto &page : state.pages_) {
    if (page.state_.load(std::memory_order_acquire) == kPageEmpty) {
      queuePage(static_cast<uint8_t>(channel), page);
    }
  }
  return true;
}

uint32_t MSamplerInstrument::findZone(uint8_t note, uint8_t velocity) const {
  if (preset_ == nullptr) {
    return InvalidZone;
  }
  for (uint16_t index = 0; index < preset_->zoneCount_; ++index) {
    const DecentSamplerZone &zone = preset_->zones_[index];
    if (note >= zone.lowNote_ && note <= zone.highNote_ &&
        velocity >= zone.lowVelocity_ && velocity <= zone.highVelocity_) {
      return index;
    }
  }
  return InvalidZone;
}

bool MSamplerInstrument::queuePage(uint8_t channel, StreamPage &page) {
  ChannelState &state = channels_[channel];
  if (!state.active_ || state.zoneIndex_ == InvalidZone ||
      state.nextReadFrame_ >=
          (preset_->zones_[state.zoneIndex_].endFrame_ == 0
               ? preset_->zones_[state.zoneIndex_].frameCount_
               : preset_->zones_[state.zoneIndex_].endFrame_)) {
    return false;
  }

  DecentSamplerZone &zone = preset_->zones_[state.zoneIndex_];
  const uint32_t endFrame =
      zone.endFrame_ == 0 ? zone.frameCount_ : zone.endFrame_;
  const uint32_t frames =
      std::min(StreamPageFrames, endFrame - state.nextReadFrame_);
  const uint32_t byteCount = frames * zone.channelCount_ * sizeof(int16_t);
  const uint64_t byteOffset = static_cast<uint64_t>(zone.dataOffset_) +
                              static_cast<uint64_t>(state.nextReadFrame_) *
                                  zone.channelCount_ * sizeof(int16_t);
  if (byteOffset > UINT32_MAX || byteCount > StreamPageBytes) {
    return false;
  }

  page.startFrame_ = state.nextReadFrame_;
  page.frameCount_ = frames;
  page.generation_ = state.generation_;
  if (!SampleStreamService::Read(zone.path_, static_cast<uint32_t>(byteOffset),
                                 page.data_, byteCount, page.state_,
                                 pendingReads_)) {
    return false;
  }
  state.nextReadFrame_ += frames;
  return true;
}

void MSamplerInstrument::servicePages(uint8_t channel, uint32_t frame) {
  ChannelState &state = channels_[channel];
  for (auto &page : state.pages_) {
    const uint8_t status = page.state_.load(std::memory_order_acquire);
    if (status == kPageLoading) {
      continue;
    }
    if (page.generation_ != state.generation_ ||
        (status == kPageReady &&
         frame >= page.startFrame_ + page.frameCount_)) {
      page.state_.store(kPageEmpty, std::memory_order_release);
    }
    if (page.state_.load(std::memory_order_relaxed) == kPageEmpty) {
      page.data_ = reinterpret_cast<int16_t *>(
          arena_ + streamBase_ +
          (static_cast<uint32_t>(channel) * PagesPerChannel +
           static_cast<uint32_t>(&page - state.pages_.data())) *
              StreamPageBytes);
      queuePage(channel, page);
    }
  }
}

MSamplerInstrument::SampleSpan
MSamplerInstrument::findSpan(uint8_t channel, uint32_t frame) const {
  const ChannelState &state = channels_[channel];
  const auto &zone = preset_->zones_[state.zoneIndex_];
  const uint32_t attackEnd = zone.startFrame_ + zone.attackFrames_;
  if (frame >= zone.startFrame_ && frame < attackEnd) {
    return {.data_ =
                reinterpret_cast<const int16_t *>(arena_ + zone.attackOffset_),
            .begin_ = zone.startFrame_,
            .end_ = attackEnd};
  }
  for (const auto &page : state.pages_) {
    if (page.state_.load(std::memory_order_acquire) == kPageReady &&
        page.generation_ == state.generation_ && frame >= page.startFrame_ &&
        frame < page.startFrame_ + page.frameCount_) {
      return {.data_ = page.data_,
              .begin_ = page.startFrame_,
              .end_ = page.startFrame_ + page.frameCount_};
    }
  }
  return {};
}

bool MSamplerInstrument::Render(int channel, AudioSample *buffer, int size,
                                bool updateTick) {
  (void)updateTick;
  if (buffer == nullptr || size <= 0 || channel < 0 ||
      channel >= static_cast<int>(channels_.size())) {
    return false;
  }
  ChannelState &state = channels_[static_cast<size_t>(channel)];
  if (!state.active_ || state.zoneIndex_ == InvalidZone || preset_ == nullptr) {
    return false;
  }
  const uint8_t voice = static_cast<uint8_t>(channel);
  const auto &zone = preset_->zones_[state.zoneIndex_];
  const uint32_t endFrame =
      zone.endFrame_ == 0 ? zone.frameCount_ : zone.endFrame_;
  const uint32_t channels = zone.channelCount_;
  servicePages(voice, static_cast<uint32_t>(state.position_));
  SampleSpan span{};
  for (int32_t index = 0; index < size; ++index) {
    const uint32_t frame = static_cast<uint32_t>(state.position_);
    float left = state.lastLeft_;
    float right = state.lastRight_;
    if (frame >= endFrame || state.starved_) {
      if (state.releaseSamples_ == 0) {
        state.releaseSamples_ = ReleaseRampSamples;
      }
    } else {
      if (span.data_ == nullptr || frame < span.begin_ || frame >= span.end_) {
        // Only inspect atomic page state at block/span boundaries. Never
        // recycle the current page while the interpolation neighbour is read.
        servicePages(voice, frame);
        span = findSpan(voice, frame);
      }
      if (span.data_ == nullptr) {
        state.starved_ = true;
        state.releaseSamples_ = ReleaseRampSamples;
#if defined(ADV)
        streamingPerf.pageMisses = streamingPerf.pageMisses + 1;
#endif
      } else {
        const int16_t *source = span.data_ + (frame - span.begin_) * channels;
        left = source[0] / kPcmScale;
        right = channels == 2 ? source[1] / kPcmScale : left;
        const uint32_t nextFrame = std::min(frame + 1, endFrame - 1);
        const int16_t *next = source;
        if (nextFrame < span.end_) {
          next = span.data_ + (nextFrame - span.begin_) * channels;
        } else {
          const SampleSpan adjacent = findSpan(voice, nextFrame);
          if (adjacent.data_ != nullptr) {
            next = adjacent.data_ + (nextFrame - adjacent.begin_) * channels;
          } else {
#if defined(ADV)
            streamingPerf.interpolationMisses =
                streamingPerf.interpolationMisses + 1;
#endif
          }
        }
        const float fraction = state.position_ - static_cast<float>(frame);
        left += (next[0] / kPcmScale - left) * fraction;
        const float nextRight =
            channels == 2 ? next[1] / kPcmScale : next[0] / kPcmScale;
        right += (nextRight - right) * fraction;
        state.position_ += state.speed_;
        state.lastLeft_ = left;
        state.lastRight_ = right;
      }
    }
    float gain = state.level_;
    if (state.releaseSamples_ > 0) {
      gain *= static_cast<float>(state.releaseSamples_) / ReleaseRampSamples;
      --state.releaseSamples_;
      if (state.releaseSamples_ == 0) {
        state.active_ = false;
      }
    }
    buffer[index * 2] = left * gain;
    buffer[index * 2 + 1] = right * gain;
    if (!state.active_) {
      std::fill(buffer + (index + 1) * 2, buffer + size * 2, 0.0f);
      break;
    }
  }
  if (state.active_ && !state.starved_) {
    servicePages(voice, static_cast<uint32_t>(state.position_));
  }
  return true;
}

void MSamplerInstrument::SetPitchMultiplier(int channel, float multiplier) {
  if (channel < 0 || channel >= static_cast<int>(channels_.size())) {
    return;
  }
  ChannelState &state = channels_[static_cast<size_t>(channel)];
  state.speed_ = std::clamp(state.baseSpeed_ * multiplier, 0.25f, 4.0f);
}

AudioBufferLayout MSamplerInstrument::GetAudioBufferLayout(int channel) const {
  (void)channel;
  return AudioBufferLayout::StereoInterleaved;
}

bool MSamplerInstrument::GetPlaybackPosition(int channel,
                                             uint32_t &position) const {
  if (channel < 0 || channel >= static_cast<int>(channels_.size())) {
    position = 0;
    return false;
  }
  const ChannelState &state = channels_[static_cast<size_t>(channel)];
  if (!state.active_) {
    position = 0;
    return false;
  }
  position = static_cast<uint32_t>(state.position_);
  return true;
}

void MSamplerInstrument::Release(int channel) {
  // AudioVoice owns note-off gain and stops this source when its ADSR is idle.
  // Keep advancing the sample throughout that release, rather than replacing
  // the user-selected envelope with the emergency end/starvation ramp.
  (void)channel;
}

void MSamplerInstrument::Stop(int channel) {
  if (channel >= 0 && channel < static_cast<int>(channels_.size())) {
    channels_[static_cast<size_t>(channel)].active_ = false;
  }
}

void MSamplerInstrument::ProcessCommand(int channel, TrackerCommand command,
                                        ushort value) {
  (void)channel;
  (void)command;
  (void)value;
}

int MSamplerInstrument::GetTable() { return table_.GetInt(); }

bool MSamplerInstrument::GetTableAutomation() {
  return tableAutomation_.GetBool();
}

void MSamplerInstrument::GetTableState(TableSaveState &state) {
  state = tableState_;
}

void MSamplerInstrument::SetTableState(TableSaveState &state) {
  tableState_ = state;
}

void MSamplerInstrument::SaveContent(tinyxml2::XMLPrinter *printer) {
  if (!presetPath_.empty()) {
    printer->PushAttribute("PRESET", presetPath_.c_str());
  }
  I_Instrument::SaveContent(printer);
}

void MSamplerInstrument::RestoreContent(PersistencyDocument *doc) {
  etl::string<PFILENAME_SIZE> presetPath;
  if (doc->r_ == YXML_OK || doc->r_ == YXML_ELEMSTART ||
      doc->r_ == YXML_ATTREND) {
    bool hasAttribute = doc->NextAttribute();
    while (hasAttribute) {
      if (!strcasecmp(doc->attrname_, "PRESET")) {
        presetPath = doc->attrval_;
      }
      hasAttribute = doc->NextAttribute();
    }
  }
  if (!presetPath.empty() && !LoadPreset(presetPath.c_str())) {
    Trace::Error("MSAMPLER: failed to restore preset %s", presetPath.c_str());
  }
  I_Instrument::RestoreContent(doc);
}

void MSamplerInstrument::Purge() {
  for (auto &channel : channels_) {
    channel.active_ = false;
  }
  SampleStreamService::Wait(pendingReads_);
  releaseArena();
  presetPath_.clear();
  I_Instrument::Purge();
  tableState_.Reset();
}

void MSamplerInstrument::CloneFrom(I_Instrument &src) {
  if (src.GetType() != IT_MSAMPLER) {
    return;
  }
  auto &other = static_cast<MSamplerInstrument &>(src);
  I_Instrument::CloneFrom(src);
  if (!other.presetPath_.empty()) {
    LoadPreset(other.presetPath_.c_str());
  }
  tableState_ = other.tableState_;
}

void MSamplerInstrument::clearState(ChannelState &state) {
  state.active_ = false;
  state.zoneIndex_ = InvalidZone;
  state.position_ = 0.0f;
  state.baseSpeed_ = 1.0f;
  state.speed_ = 1.0f;
  state.releaseSamples_ = 0;
}

void MSamplerInstrument::releaseArena() {
  if (arena_ != nullptr) {
    SampleStreamService::Release(pendingReads_);
    SamplePool::GetInstance()->ReleaseStreamingStorage(arena_);
    arena_ = nullptr;
    preset_ = nullptr;
    attackBase_ = 0;
    streamBase_ = 0;
  }
}
