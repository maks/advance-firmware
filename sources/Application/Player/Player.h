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

#ifndef _PLAYER_H_
#define _PLAYER_H_
#include "Application/Views/BaseClasses/ViewEvent.h"
#include "Application/Views/ViewData.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"
#include "PlayerMixer.h"
#include "RowFx.h"
#include "SelectionResampleRequest.h"
#include "Services/Midi/ControlSurface/ControlSurfaceState.h"
#include "Services/Midi/MidiNoteTracker.h"
#include "SongLoopRange.h"
#include "SyncMaster.h"
#include "System/Timer/Timer.h"
#include "config/StringLimits.h"
#include <cstdint>

enum PlayerEventType {
  PET_START,
  PET_UPDATE,
  PET_STOP,
  PET_STATE_CHANGE,
  PET_NOTE_UPDATE
};

enum SequencerMode { SM_SONG, SM_LIVE };
enum PlayerStopMode { PSM_RELEASE, PSM_PANIC };

enum QueueingMode {
  QM_NONE,
  QM_CHAINSTART,
  QM_PHRASESTART,
  QM_CHAINSTOP,
  QM_PHRASESTOP,
  QM_TICKSTART
};

typedef uint32_t MixerStereoLevel;

// Direct note sources use separate instances so their playback indicators and
// lifetimes do not get conflated.
class DirectNoteChannelState {
public:
  void StartChannel(uint8_t channel) {
    channelMask_ |= static_cast<uint32_t>(1UL << channel);
  }

  void StopChannel(uint8_t channel) {
    channelMask_ &= ~static_cast<uint32_t>(1UL << channel);
  }

  void Clear() { channelMask_ = 0; }
  bool IsActive() const { return channelMask_ != 0; }
  bool IsChannelActive(uint8_t channel) const {
    return channel < SONG_CHANNEL_COUNT &&
           (channelMask_ & static_cast<uint32_t>(1UL << channel)) != 0;
  }

private:
  uint32_t channelMask_ = 0;
};

class I_Instrument;
class PlayerEvent : public ViewEvent {
public:
  PlayerEvent(PlayerEventType type, unsigned int tickCount = 0);
  PlayerEventType GetType();
  unsigned int GetTickCount();

private:
  PlayerEventType type_;
  unsigned int tickCount_;
};

class Player : public I_Observer,
               public Observable,
               public T_Singleton<Player> {
private: // Singleton
  friend class etl::singleton<Player>;
  Player();

public:
  // Unlike GetInstance(), this does not construct the player during early
  // service initialization.
  static Player *GetExistingInstance();

  bool Init(Project *, ViewData *);
  void BindProject(Project *, ViewData *);
  void Reset();
  void Close();

  virtual void Update(Observable &o, I_ObservableData *d);

  // basic interface

  void Start(PlayMode mode, bool forceSongMode, MixerServiceMode msmMode,
             bool stopAtEnd = false,
             SongLoopRange songLoopRange = SongLoopRange());
  void Stop(PlayerStopMode stopMode = PSM_RELEASE);
  void Panic() { Stop(PSM_PANIC); }
  void Kill();
  void ConfigureSelectionRender(const SelectionResampleRequest &request);
  void ClearSelectionRender();
  bool HasSelectionRender() const;
  int GetSelectionRenderSongRow() const;

  void SetSequencerMode(SequencerMode mode);
  SequencerMode GetSequencerMode();

  void OnStartButton(PlayMode origin, unsigned int from, bool startFromLastPos,
                     unsigned char chainPos,
                     MixerServiceMode msmMode = MSM_AUDIO,
                     bool stopAtEnd = false);
  void OnSongStartButton(unsigned int from, unsigned int to, bool requestStop,
                         bool forceImmediate,
                         MixerServiceMode msmMode = MSM_AUDIO,
                         bool stopAtEnd = false, int useStartRow = -1,
                         SongLoopRange songLoopRange = SongLoopRange());
  bool ToggleLiveStartStop(MixerServiceMode msmMode = MSM_AUDIO);
  bool IsPlaying();

  bool IsRunning();
  bool IsAuditioning() const;
  bool IsMidiInputPlaying() const;
  bool IsMidiInputChannelPlaying(uint8_t channel) const;
  bool IsInstrumentInUse(const I_Instrument *instrument);
  bool IsTailPlaying();
  bool GetStopAtEnd() { return stopAtEnd_; }

  void ProcessCommands(bool profileChannels = false);
  bool ProcessChannelCommand(int channel, TrackerCommand cmd, ushort param);

  void StartStreaming(const char *name, int startSample = 0);
  void StartLoopingStreaming(const char *name);
  void StopStreaming();

  // Returns true if a file preview/streamer is active (not sequencer playback).
  bool IsStreamingActive();

  void StartRecordStreaming(uint16_t *srcBuffer, uint32_t size, bool stereo);
  void StopRecordStreaming();

  // Channel data

  bool IsChannelPlaying(int channel);
  void SetChannelMute(int channel, bool mute);
  bool IsChannelMuted(int channel);
  int GetChannelSongPlayPosition(int channel);

  // Live queuing

  QueueingMode GetQueueingMode(int i);
  unsigned char GetQueuePosition(int i);
  unsigned char GetQueueChainPosition(int i);
  void QueueChannel(int i, QueueingMode mode, unsigned char position,
                    unsigned char chainpos = 0);

  const char *GetLiveIndicator(int channel);
  double GetPlayTime();

  const char *GetPlayedNote(int channel);
  const char *GetPlayedOctive(int channel);
  const char *GetPlayedInstrument(int channel);
  bool GetPlayedSliceIndex(int channel, uint8_t &sliceIndex);
  bool GetInstrumentPlaybackPosition(I_Instrument *instrument,
                                     uint32_t &position);

  // info
  int GetPlayedBufferPercentage();

  etl::array<stereosample, SONG_CHANNEL_COUNT> *GetMixerLevels();

  // master out, last avg level while playing
  stereosample GetMasterLevel();

  etl::string<STRING_AUDIO_API_MAX> GetAudioAPI();
  etl::string<STRING_AUDIO_DEVICE_MAX> GetAudioDevice();
  int GetAudioBufferSize();
  int GetAudioRequestedBufferSize();
  int GetAudioPreBufferCount();

  Project *GetProject() { return project_; }
  ViewData *GetViewData() { return viewData_; }

  // Direct note playback methods
  void PlayNote(unsigned short instrumentIndex, unsigned short channel,
                unsigned char note, unsigned char velocity);
  void StopNote(unsigned short instrumentIndex, unsigned short channel);
  void PlayMidiNote(MidiInputSource inputSource, uint8_t midiChannel,
                    uint8_t instrumentIndex, uint8_t note, uint8_t velocity);
  void ApplyMidiVoiceCC(MidiInputSource inputSource, uint8_t midiChannel,
                        uint8_t program, uint8_t cc, uint8_t value);
  void ResetMidiVoicePickup(MidiInputSource inputSource, uint8_t midiChannel);
  void StopMidiNote(MidiInputSource inputSource, uint8_t midiChannel,
                    uint8_t note);
  void ReleaseMidiInputSource(MidiInputSource inputSource);
  void OnMidiInputAssignmentChanged(uint8_t songChannel);

  void FillControlSurfaceState(ControlSurfaceState &state, int windowOffset,
                               int matrixSize);

protected:
  void updateSongPos(int position, int channel, int chainPos = 0, int hop = -1);
  void updateChainPos(int position, int channel, int hop = 0);
  void updatePhrasePos(int pos, int channel);
  void playCursorPosition(int channel);
  void StepAutomationTableForRetrigger(int channel, I_Instrument *instrument);
  void RetriggerChannelInstrument(int channel, int semitoneOffset,
                                  bool stepAutomationTable);
  void RetriggerChannelInstrumentAtNote(int channel, int note,
                                        bool stepAutomationTable);
  int getChannelHop(int channel, int pos);
  void moveToNextStep();
  void moveToNextPhrase(int channel, int hop = -1);
  void moveToNextChain(int channel, int hop);

  void triggerLiveChains();

  bool isPlayable(int row, int col, int chainPos = 0);
  bool findPlayable(uchar *row, int col, uchar chainPos = 0);

private:
  struct SelectionRenderState {
    bool active = false;
    SelectionResampleSourceKind sourceKind = SelectionResampleSourceKind::Song;
    uint8_t startSongRow = 0;
    uint8_t endSongRow = 0;
    uint8_t startChainRow = 0;
    uint8_t endChainRow = 0;
    uint8_t startPhraseRow = 0;
    uint8_t endPhraseRow = 0;
    uint8_t trackerChannel = 0;
    uint16_t songChannelMask = 0;
    uint8_t currentSongRow = 0;
  };

  void ResetLastActiveCommands();
  void RecordLastActiveCommand(int channel, TrackerCommand cmd, ushort param);
  void PlayDirectNote(unsigned short instrumentIndex, unsigned short channel,
                      unsigned char note, unsigned char velocity,
                      DirectNoteChannelState &state);
  void StopDirectNote(unsigned short channel, DirectNoteChannelState &state);
  void stopLocked(PlayerStopMode stopMode);
  bool shouldStopSelectionRenderAtChainRow(int channel, int nextChainRow) const;
  bool shouldStopSelectionRenderAtPhraseRow(int channel,
                                            int nextPhraseRow) const;

  PlayerMixer mixer_;
  ViewData *viewData_;
  Project *project_;

  SequencerMode sequencerMode_;
  PlayMode mode_;
  bool isRunning_;
  bool stopAtEnd_;
  DirectNoteChannelState directNoteAuditionState_;
  DirectNoteChannelState midiInputNoteState_;
  MidiNoteTracker midiNoteTracker_;

  unsigned long startClock_; // .Used to time display live queued chains
                             //  for blinking effect
  unsigned long now_;
  int lastPercentage_;
  unsigned int lastBeatCount_;
  unsigned char lastSongPos_;
  bool firstPlayCycle_;
  bool triggerLiveChains_;

  double startTime_;

  char instrumentOnChannel_[SONG_CHANNEL_COUNT][3];

  // Live queuing system

  unsigned char liveQueuePosition_[SONG_CHANNEL_COUNT];
  QueueingMode liveQueueingMode_[SONG_CHANNEL_COUNT];
  unsigned char liveQueueChainPosition_[SONG_CHANNEL_COUNT];
  uint32_t timeToLive_[SONG_CHANNEL_COUNT];
  uint32_t timeToRelease_[SONG_CHANNEL_COUNT];
  uint32_t timeToStart_[SONG_CHANNEL_COUNT];

  bool retrigAllImmediate_;
  unsigned char retrigPos_;
  SongLoopRange songLoopRange_;
  SelectionRenderState selectionRender_;
  bool selectionRenderSongRowAdvanced_ = false;
  PlayerLastActiveCommandState lastActiveCommands_[SONG_CHANNEL_COUNT];
  etl::vector<I_Observer *, 8> observers_;
};

#endif
