/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_MSAMPLER_INSTRUMENT_H
#define PICOTRACKER_MSAMPLER_INSTRUMENT_H

#include "Application/Instruments/DecentSamplerPreset.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/array.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/Variables/Variable.h"
#include <array>
#include <atomic>
#include <cstdint>

class MSamplerInstrument : public I_Instrument {
public:
  static constexpr uint32_t ArenaBytes = 4U * 1024U * 1024U;
  static constexpr uint32_t AttackCacheBytes = 3U * 1024U * 1024U;
  static constexpr uint32_t StreamPageBytes = 32U * 1024U;
  static constexpr uint32_t StreamPageFrames = 8192;
  static constexpr uint8_t PagesPerChannel = 2;

  MSamplerInstrument();
  ~MSamplerInstrument() override;

  bool Init() override;
  bool Start(int channel, unsigned char note,
             InstrumentStartMode mode = InstrumentStartMode::NewNote) override;
  bool StartWithVelocity(
      int channel, unsigned char note, uint8_t velocity,
      InstrumentStartMode mode = InstrumentStartMode::NewNote) override;
  void Release(int channel) override;
  void Stop(int channel) override;
  void OnStart() override;
  bool IsInitialized() override;
  bool IsEmpty() override;
  InstrumentType GetType() override { return IT_MSAMPLER; }
  bool Render(int channel, AudioSample *buffer, int size,
              bool updateTick) override;
  void SetPitchMultiplier(int channel, float multiplier) override;
  bool GetPlaybackPosition(int channel, uint32_t &position) const override;
  AudioBufferLayout GetAudioBufferLayout(int channel) const override;
  void ProcessCommand(int channel, TrackerCommand command,
                      ushort value) override;
  int GetTable() override;
  bool GetTableAutomation() override;
  void GetTableState(TableSaveState &state) override;
  void SetTableState(TableSaveState &state) override;
  etl::ivector<Variable *> *Variables() override { return &variables_; }
  void SaveContent(tinyxml2::XMLPrinter *printer) override;
  void RestoreContent(PersistencyDocument *doc) override;
  void Purge() override;
  void CloneFrom(I_Instrument &src) override;

  bool LoadPreset(const char *path);
  const char *GetPresetPath() const { return presetPath_.c_str(); }

private:
  struct StreamPage {
    std::atomic<uint8_t> state_{0}; // empty, loading, ready, failed
    uint32_t startFrame_ = 0;
    uint32_t frameCount_ = 0;
    uint32_t generation_ = 0;
    int16_t *data_ = nullptr;
  };

  struct ChannelState {
    std::array<StreamPage, PagesPerChannel> pages_{};
    uint32_t generation_ = 0;
    uint32_t zoneIndex_ = UINT32_MAX;
    uint32_t nextReadFrame_ = 0;
    float position_ = 0.0f;
    float baseSpeed_ = 1.0f;
    float speed_ = 1.0f;
    float level_ = 1.0f;
    float lastLeft_ = 0.0f;
    float lastRight_ = 0.0f;
    uint8_t releaseSamples_ = 0;
    bool active_ = false;
  };

  static constexpr uint8_t ReleaseRampSamples = 32;
  static constexpr uint32_t InvalidZone = UINT32_MAX;

  uint32_t findZone(uint8_t note, uint8_t velocity) const;
  bool queuePage(uint8_t channel, StreamPage &page);
  bool readFrame(uint8_t channel, uint32_t frame, float &left, float &right);
  void clearState(ChannelState &state);
  void releaseArena();

  etl::vector<Variable *, 3> variables_;
  Variable level_;
  Variable table_;
  Variable tableAutomation_;
  TableSaveState tableState_;
  etl::string<PFILENAME_SIZE> presetPath_;
  uint8_t *arena_ = nullptr;
  DecentSamplerPreset *preset_ = nullptr;
  uint32_t attackBase_ = 0;
  uint32_t streamBase_ = 0;
  std::atomic<uint16_t> pendingReads_{0};
  std::array<ChannelState, SONG_CHANNEL_COUNT> channels_{};
};

#endif
