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

#include "Player.h"
#include "Application/Effects/DelayOutputEffect.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Groove.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/char.h"
#include "Application/Views/BaseClasses/ViewEvent.h"
#include "PlayerMixer.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/n_assert.h"
#include "System/Profiler/DWTProfiler.h"
#include "System/System/System.h"
#include "System/io/Status.h"
#include "config/MemorySections.h"
#include <algorithm>
#include <math.h>
#include <string.h>

namespace {

FourCC GetReverbCommandParameterId(TrackerCommand command) {
  switch (command) {
  case TrackerCommand::InstrumentCommandReverbPreDelay:
    return FourCC::VarReverbPreDelay;
  case TrackerCommand::InstrumentCommandReverbInputTone:
    return FourCC::VarReverbInputTone;
  case TrackerCommand::InstrumentCommandReverbDiffuse:
    return FourCC::VarReverbDiffuse;
  case TrackerCommand::InstrumentCommandReverbDecay:
    return FourCC::VarReverbDecay;
  case TrackerCommand::InstrumentCommandReverbTailTone:
    return FourCC::VarReverbTailTone;
  case TrackerCommand::InstrumentCommandReverbModulationSpeed:
    return FourCC::VarReverbModulationRate;
  case TrackerCommand::InstrumentCommandReverbModulationLevel:
    return FourCC::VarReverbModulationDepth;
  default:
    return FourCC::Default;
  }
}

DWTProfileStats gPlayerControlProfile = {
    .name_ = "player control total",
    .printIndividually_ = false,
};

void registerPlayerProfiles() {
  static bool registered = false;
  if (registered) {
    return;
  }

  // This scope measures once-per-buffer player control work: note starts,
  // phrase commands, table stepping, and retrigger/release handling.
  RegisterDWTProfileStats(&gPlayerControlProfile);
  registered = true;
}

} // namespace

int32_t DecodeRetriggerOffset(int32_t value) {
  return static_cast<int8_t>(value & 0xFF);
}

// Private constructor - Singleton

Player::Player() : Observable(&observers_), mixer_() {
  registerPlayerProfiles();

  isRunning_ = false;
  viewData_ = 0;

  lastSongPos_ = 0;
  mode_ = PM_SONG;
  sequencerMode_ = SM_SONG;
  lastPercentage_ = 0;
  retrigAllImmediate_ = false;

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    instrumentOnChannel_[i][0] = ' ';
    instrumentOnChannel_[i][1] = ' ';
    instrumentOnChannel_[i][2] = '\0';
    timeToLive_[i] = 0;
    timeToRelease_[i] = 0;
    timeToStart_[i] = 0;
  }
};

Player *Player::GetExistingInstance() {
  if (!etl::singleton<Player>::is_valid()) {
    return nullptr;
  }
  return &etl::singleton<Player>::instance();
}

bool Player::Init(Project *project, ViewData *viewData) {

  viewData_ = viewData;
  project_ = project;

  if (!mixer_.Init(project)) {
    return false;
  }

  mixer_.AddObserver((*this));
  SyncMaster *sync = SyncMaster::GetInstance();
  sync->SetTempo(project_->GetTempo());
  return mixer_.Start();
}

void Player::BindProject(Project *project, ViewData *viewData) {
  viewData_ = viewData;
  project_ = project;
  midiInputNoteState_.Clear();
  midiNoteTracker_.clear();
  mixer_.BindProject(project);

  SyncMaster *sync = SyncMaster::GetInstance();
  sync->SetTempo(project_->GetTempo());
}

void Player::Reset() {
  directNoteAuditionState_.Clear();
  midiInputNoteState_.Clear();
  viewData_ = 0;
  project_ = 0;
  mixer_.RemoveObserver(*this);
  Close();
};

void Player::Close() {
  mixer_.Stop();
  mixer_.Close();
};

void Player::SetChannelMute(int channel, bool mute) {
  mixer_.SetChannelMute(channel, mute);
};

bool Player::IsChannelMuted(int channel) {
  return mixer_.IsChannelMuted(channel);
};

void Player::Start(PlayMode mode, bool forceSongMode, MixerServiceMode msmMode,
                   bool stopAtEnd, SongLoopRange songLoopRange) {

  mixer_.Lock();
  directNoteAuditionState_.Clear();
  midiInputNoteState_.Clear();
  midiNoteTracker_.clear();

  lastBeatCount_ = 0;
  stopAtEnd_ = stopAtEnd;

  // Get start time for clock

  System *system = System::GetInstance();
  now_ = startClock_ = system->GetClock();

  // Sets play mode.
  // DO I need playMode_ in view data ?
  // Seems like duplicate with mode_

  viewData_->playMode_ = (forceSongMode ? PM_SONG : mode);

  songLoopRange_ = SongLoopRange();
  if (!selectionRender_.active && viewData_->playMode_ == PM_SONG &&
      IsSongLoopRangeValid(songLoopRange)) {
    songLoopRange_ = songLoopRange;
  }

  // See if we start from current song position
  // or from last stored

  unsigned playPos = viewData_->songY_ + viewData_->songOffset_;
  if (selectionRender_.active) {
    playPos = selectionRender_.startSongRow;
  } else if (songLoopRange_.active) {
    playPos = songLoopRange_.startRow;
  }

  if (forceSongMode == false) {
    lastSongPos_ = playPos;
  } else {
    playPos = lastSongPos_;
  }

  // Clear all channel based data

  const int channelCount = SONG_CHANNEL_COUNT;
  for (int i = 0; i < channelCount; i++) {
    mixer_.StopChannel(i);
    timeToLive_[i] = 0;
    timeToRelease_[i] = 0;
    timeToStart_[i] = 0;
    TablePlayback &tpb = TablePlayback::GetTablePlayback(i);
    TablePlayback &atp = TablePlayback::GetAutomationPlayback(i);
    tpb.Stop();
    atp.Stop();
  }

  // Tell the instruments we're starting

  project_->GetInstrumentBank()->OnStart();

  Groove::GetInstance()->Reset();

  // Let's get started !

  SyncMaster::GetInstance()->Start();
  // Playback and file rendering leave the codec output enabled.

  firstPlayCycle_ = true;
  mode_ = viewData_->playMode_;

  // Notify MixerService about player start - this will set up rendering if
  // needed
  mixer_.OnPlayerStart(msmMode);

  MidiService *ms = MidiService::GetInstance();
  ms->OnPlayerStart();

  switch (viewData_->playMode_) {
  case PM_SONG: {
    for (int i = 0; i < channelCount; i++) {
      if (!ShouldPlaySongLoopChannel(songLoopRange_, i) ||
          viewData_->song_->IsMidiInputChannelAssigned(i)) {
        viewData_->currentPlayChain_[i] = EMPTY_SONG_VALUE;
        viewData_->currentPlayPhrase_[i] = EMPTY_SONG_VALUE;
        continue;
      }
      mixer_.StartChannel(i);
      updateSongPos(playPos, i);
    }
  } break;

  case PM_LIVE: {
    for (int i = 0; i < channelCount; i++) {
      if (!viewData_->song_->IsMidiInputChannelAssigned(i) &&
          ((liveQueueingMode_[i] == QM_CHAINSTART) ||
           (liveQueueingMode_[i] == QM_PHRASESTART) ||
           (liveQueueingMode_[i] == QM_TICKSTART))) {
        mixer_.StartChannel(i);
        updateSongPos(liveQueuePosition_[i], i, liveQueueChainPosition_[i]);
        liveQueueingMode_[i] = QM_NONE;
      }
    }
  } break;

  case PM_CHAIN:
  case PM_PHRASE: {
    int currentChannel = viewData_->songX_;
    if (selectionRender_.active) {
      currentChannel = selectionRender_.trackerChannel;
    }
    if (viewData_->song_->IsMidiInputChannelAssigned(currentChannel)) {
      break;
    }
    mixer_.StartChannel(currentChannel);
    int currentChainPos = viewData_->chainRow_;
    if (selectionRender_.active) {
      currentChainPos = selectionRender_.startChainRow;
    }
    int currentPhrasePos = 0;
    if (selectionRender_.active &&
        selectionRender_.sourceKind == SelectionResampleSourceKind::Phrase) {
      currentPhrasePos = selectionRender_.startPhraseRow;
    }
    updateSongPos(playPos, currentChannel, currentChainPos, currentPhrasePos);
  } break;

  case PM_AUDITION: {
    int currentChannel = viewData_->songX_;
    if (viewData_->song_->IsMidiInputChannelAssigned(currentChannel)) {
      break;
    }
    mixer_.StartChannel(currentChannel);

    int currentChainPos = viewData_->chainRow_;
    int currentPhrasePos = viewData_->phraseCurPos_;
    // uses hop for PhrasePos
    updateSongPos(playPos, currentChannel, currentChainPos, currentPhrasePos);
  } break;

  default:
    NInvalid;
    break;
  }

  ProcessCommands();

  startTime_ = mixer_.GetAudioOut()->GetStreamTime();

  SetChanged();
  PlayerEvent pe(PET_START);
  NotifyObservers(&pe);

  isRunning_ = true; // keep last !!!!

  mixer_.Unlock();
}

void Player::stopLocked(PlayerStopMode stopMode) {
  // Leave the codec output enabled so voice releases and FX tails can finish.

  if (stopMode == PSM_PANIC) {
    mixer_.Panic();
    const int channelCount = SONG_CHANNEL_COUNT;
    for (int i = 0; i < channelCount; i++) {
      TablePlayback &tpb = TablePlayback::GetTablePlayback(i);
      TablePlayback &atp = TablePlayback::GetAutomationPlayback(i);
      tpb.Stop();
      atp.Stop();
    }
  } else {
    const int channelCount = SONG_CHANNEL_COUNT;
    for (int i = 0; i < channelCount; i++) {
      mixer_.ReleaseChannel(i);
      TablePlayback &tpb = TablePlayback::GetTablePlayback(i);
      TablePlayback &atp = TablePlayback::GetAutomationPlayback(i);
      tpb.Stop();
      atp.Stop();
    }
  }
  MidiService::GetInstance()->OnPlayerStop();
  mixer_.OnPlayerStop();

  SyncMaster::GetInstance()->Stop();
  songLoopRange_ = SongLoopRange();
  ClearSelectionRender();
  directNoteAuditionState_.Clear();
  midiInputNoteState_.Clear();
  midiNoteTracker_.clear();
  isRunning_ = false;
  SetChanged();
  PlayerEvent pe(PET_STOP);
  NotifyObservers(&pe);
}

void Player::Stop(PlayerStopMode stopMode) {
  mixer_.Lock();
  stopLocked(stopMode);
  mixer_.Unlock();
}

void Player::Kill() {
  mixer_.Lock();
  if (isRunning_) {
    stopLocked(PSM_PANIC);
  } else {
    mixer_.Panic();
    directNoteAuditionState_.Clear();
    midiInputNoteState_.Clear();
    midiNoteTracker_.clear();
  }
  mixer_.Unlock();
}

void Player::ConfigureSelectionRender(const SelectionResampleRequest &request) {
  selectionRender_.active = true;
  selectionRender_.sourceKind = request.sourceKind;
  selectionRender_.startSongRow = request.startSongRow;
  selectionRender_.endSongRow = request.endSongRow;
  selectionRender_.startChainRow = request.startChainRow;
  selectionRender_.endChainRow = request.endChainRow;
  selectionRender_.startPhraseRow = request.startPhraseRow;
  selectionRender_.endPhraseRow = request.endPhraseRow;
  selectionRender_.trackerChannel = request.trackerChannel;
  selectionRender_.songChannelMask = request.GetSongChannelMask();
  selectionRender_.currentSongRow = request.startSongRow;
  selectionRenderSongRowAdvanced_ = false;
}

void Player::ClearSelectionRender() {
  selectionRender_ = SelectionRenderState();
}

bool Player::HasSelectionRender() const { return selectionRender_.active; }

int Player::GetSelectionRenderSongRow() const {
  return static_cast<int>(selectionRender_.currentSongRow);
}

bool Player::shouldStopSelectionRenderAtChainRow(int channel,
                                                 int nextChainRow) const {
  if (!selectionRender_.active ||
      selectionRender_.sourceKind != SelectionResampleSourceKind::Chain) {
    return false;
  }

  return channel == selectionRender_.trackerChannel &&
         nextChainRow > selectionRender_.endChainRow;
}

bool Player::shouldStopSelectionRenderAtPhraseRow(int channel,
                                                  int nextPhraseRow) const {
  if (!selectionRender_.active ||
      selectionRender_.sourceKind != SelectionResampleSourceKind::Phrase) {
    return false;
  }

  return channel == selectionRender_.trackerChannel &&
         nextPhraseRow > selectionRender_.endPhraseRow;
}

const char *Player::GetPlayedNote(int channel) {
  return mixer_.GetPlayedNote(channel);
}

const char *Player::GetPlayedOctive(int channel) {
  return mixer_.GetPlayedOctive(channel);
}

const char *Player::GetPlayedInstrument(int channel) {
  if ((mixer_.GetPlayedOctive(channel))[1] == ' ') {
    return mixer_.GetPlayedOctive(channel);
  } else {
    if (!IsChannelMuted(channel)) {
      return (char *)(&(instrumentOnChannel_[channel][0]));
    } else {
      return "--";
    }
  }
}

bool Player::GetPlayedSliceIndex(int channel, uint8_t &sliceIndex) {
  return mixer_.GetPlayedSliceIndex(channel, sliceIndex);
}

bool Player::GetInstrumentPlaybackPosition(I_Instrument *instrument,
                                           uint32_t &position) {
  return mixer_.GetInstrumentPlaybackPosition(instrument, SONG_CHANNEL_COUNT,
                                              position);
}

const char *Player::GetLiveIndicator(int channel) {

  bool blink = true;

  switch (liveQueueingMode_[channel]) {
  case QM_CHAINSTART:
  case QM_CHAINSTOP:
    blink = (now_ - startClock_) % 500 < 250;
    break;
  case QM_PHRASESTART:
  case QM_PHRASESTOP:
    blink = (now_ - startClock_) % 125 < 72;
    break;
  case QM_TICKSTART:
    blink = (now_ - startClock_) % 75 < 37;
    break;
  case QM_NONE:
    break;
  };
  if (blink) {
    switch (liveQueueingMode_[channel]) {
    case QM_CHAINSTART:
    case QM_PHRASESTART:
    case QM_TICKSTART:
      if (!IsChannelMuted(channel)) {
        return (">");
      } else {
        return ("-");
      }
      break;
    case QM_CHAINSTOP:
    case QM_PHRASESTOP:
      return "_";
      break;
    case QM_NONE:
      break;
    }
  }
  return " ";
};

void Player::SetSequencerMode(SequencerMode mode) {
  const bool modeChanged = (sequencerMode_ != mode);
  if (mode == SM_LIVE) {
    songLoopRange_ = SongLoopRange();
  }
  if (isRunning_) {
    switch (mode) {
    case SM_LIVE:
      mode_ = PM_LIVE;
      break;
    case SM_SONG:
      mode_ = PM_SONG;
      break;
    };
  };
  sequencerMode_ = mode;

  if (modeChanged) {
    if (mode == SM_LIVE) {
      MidiService::GetInstance()->RefreshControlSurfaceSelection();
    }
    SetChanged();
    PlayerEvent pe(PET_STATE_CHANGE);
    NotifyObservers(&pe);
  }
};

SequencerMode Player::GetSequencerMode() { return sequencerMode_; };

bool Player::IsChannelPlaying(int channel) {
  return mixer_.IsChannelPlaying(channel);
};

int Player::GetChannelSongPlayPosition(int channel) {
  if ((viewData_ == nullptr) || (channel < 0) ||
      (channel >= SONG_CHANNEL_COUNT)) {
    return -1;
  }
  return viewData_->songPlayPos_[channel];
}

// Handles start button on any screen BUT the song screen

void Player::OnStartButton(PlayMode origin, unsigned int from,
                           bool startFromPrevious, unsigned char chainPos,
                           MixerServiceMode msmMode, bool stopAtEnd) {

  switch (GetSequencerMode()) {

  case SM_SONG:

    // If sequencer not running, start otherwise stop

    if (isRunning_ && viewData_->playMode_ != PM_AUDITION) {
      Stop();
    } else {
      const int channelCount = SONG_CHANNEL_COUNT;
      for (int i = 0; i < channelCount; i++) {
        liveQueueingMode_[i] = QM_NONE;
      };
      Start(origin, startFromPrevious, msmMode);
    }
    break;
  case SM_LIVE: // doesn't make much sense here
    break;
  }
}

// Handles start on song screen
void Player::OnSongStartButton(unsigned int from, unsigned int to,
                               bool requestStop, bool forceImmediate,
                               MixerServiceMode msmMode, bool stopAtEnd,
                               int useStartRow, SongLoopRange songLoopRange) {

  switch (GetSequencerMode()) {

  case SM_SONG:

    // If sequencer not running, start otherwise stop

    if (isRunning_ && viewData_->playMode_ != PM_AUDITION) {
      if (!forceImmediate) {
        Stop();
      } else {
        // Get current song row and queue for immediate retrigger
        retrigPos_ = viewData_->songY_ + viewData_->songOffset_;
        retrigAllImmediate_ = true;
      }
    } else {
      const int channelCount = SONG_CHANNEL_COUNT;
      for (int i = 0; i < channelCount; i++) {
        liveQueueingMode_[i] = QM_NONE;
      };
      Start(PM_SONG, false, msmMode, false, songLoopRange);
    }
    break;

  case SM_LIVE:

    // Get current song row
    unsigned char songPos = viewData_->songY_ + viewData_->songOffset_;
    if ((useStartRow >= 0) && (useStartRow < SONG_ROW_COUNT)) {
      songPos = static_cast<unsigned char>(useStartRow);
    }

    const unsigned int channelCount = SONG_CHANNEL_COUNT;
    if ((channelCount <= 0) || (from >= channelCount)) {
      break;
    }

    if (to >= channelCount) {
      to = (channelCount - 1);
    }
    if (from > to) {
      break;
    }

    if (!IsRunning()) {

      // not playing; we queue the chains in the selection
      // that contain something then start the player

      for (unsigned int i = 0; i < channelCount; i++) {
        if ((i < from) || (i > to)) {
          QueueChannel(i, QM_NONE, 0);
        } else {
          if (isPlayable(songPos, i, 0)) {
            QueueChannel(i, QM_CHAINSTART, songPos, 0);
          }
        }
      };

      Start(PM_LIVE, false, msmMode);

    } else { // Player already running

      // Queue all chain in the given selection

      for (unsigned int i = from; i <= to; i++) {

        QueueingMode mode = QM_NONE;

        uchar row = songPos;

        if (!requestStop) {
          if (findPlayable(&row, i, 0)) {
            if (!forceImmediate) {
              if ((liveQueueingMode_[i] != QM_CHAINSTART) ||
                  (liveQueuePosition_[i] != row)) {
                mode = QM_CHAINSTART;
              } else {
                mode = QM_PHRASESTART;
              }
            } else {
              mode = QM_TICKSTART;
            }
          }
        } else { // modifier = onStop from song screen
          if (GetQueueingMode(i) != QM_CHAINSTOP) {
            mode = QM_CHAINSTOP;
          } else {
            mode = QM_PHRASESTOP;
          }
        }
        if (mode != QM_NONE) {
          QueueChannel(i, mode, row, 0);
        }
      }
    };
    break;
  }
}

bool Player::ToggleLiveStartStop(MixerServiceMode msmMode) {
  if ((GetSequencerMode() != SM_LIVE) || (viewData_ == nullptr)) {
    return false;
  }

  if (IsRunning()) {
    Stop();
    return true;
  }

  const int channelCount = SONG_CHANNEL_COUNT;
  if (channelCount <= 0) {
    return false;
  }

  const unsigned char songRow =
      static_cast<unsigned char>(viewData_->songY_ + viewData_->songOffset_);
  OnSongStartButton(0, static_cast<unsigned int>(channelCount - 1), false,
                    false, msmMode, false, songRow);
  return true;
}

bool Player::IsRunning() { return isRunning_; };

stereosample Player::GetMasterLevel() { return mixer_.GetMasterOutLevel(); }

bool Player::isPlayable(int row, int col, int chainPos) {
  if (viewData_->song_->IsMidiInputChannelAssigned(col)) {
    return false;
  }
  const int channelCount = SONG_CHANNEL_COUNT;
  uchar *chain = viewData_->song_->data_ + channelCount * row + col;
  if (*chain != 0xFF) {
    uchar data = viewData_->song_->chain_.data_[16 * (*chain) + chainPos];
    return (data != 0xFF);
  }
  return false;
}

bool Player::findPlayable(uchar *row, int col, uchar chainPos) {

  if (viewData_->song_->IsMidiInputChannelAssigned(col)) {
    return false;
  }

  // first look if current is fine

  const int channelCount = SONG_CHANNEL_COUNT;
  uchar *chain = viewData_->song_->data_ + channelCount * (*row) + col;
  if (*chain != 0xFF) {
    uchar data = viewData_->song_->chain_.data_[16 * (*chain) + chainPos];
    return (data != 0xFF);
  }

  // Find upwards the first non blank

  while (*row != 255) {
    chain = viewData_->song_->data_ + channelCount * (*row) + col;
    if (*chain != 0xFF) {
      break;
    }
    *row -= 1;
  }

  if (*row == 255)
    return false;

  // Find upwards the first blank

  while (*row != 255) {
    chain = viewData_->song_->data_ + channelCount * (*row) + col;
    if (*chain == 0xFF) {
      break;
    }
    *row -= 1;
  }

  if (*row == 255) {
    *row = 0;
  } else {
    *row += 1;
  }
  chain = viewData_->song_->data_ + channelCount * (*row) + col;
  uchar data = 0xFF;
  if (*chain != 0xFF) {
    data = viewData_->song_->chain_.data_[16 * (*chain) + chainPos];
  }
  return (data != 0xFF);
}

QueueingMode Player::GetQueueingMode(int i) { return liveQueueingMode_[i]; };

void Player::FillControlSurfaceState(ControlSurfaceState &state,
                                     int windowOffset, int matrixSize) {
  state = {};
  state.isLiveMode = (sequencerMode_ == SM_LIVE);
  state.isRunning = isRunning_;

  if (!state.isLiveMode || project_ == nullptr || viewData_ == nullptr) {
    return;
  }

  const Song &song = project_->song_;
  const int songChannelCount = SONG_CHANNEL_COUNT;
  const unsigned char *songData = song.data_;
  const unsigned char *chainData = song.chain_.data_;

  if (songData != nullptr) {
    const int maxChan =
        (songChannelCount < matrixSize) ? songChannelCount : matrixSize;
    const unsigned char *data = songData + (songChannelCount * windowOffset);

    for (int row = 0; row < matrixSize; ++row) {
      if (windowOffset + row >= SONG_ROW_COUNT)
        break;

      for (int channel = 0; channel < maxChan; ++channel) {
        const unsigned char cellVal = *data++;
        if (cellVal == EMPTY_SONG_VALUE) {
          continue;
        }

        if (cellVal == NOTE_OFF) {
          state.cells[channel][row] = ControlSurfaceState::Cell::NoteOff;
          continue;
        }

        const bool chainHasPlayableFirstPhrase =
            chainData[(16 * cellVal)] != EMPTY_SONG_VALUE;
        if (!chainHasPlayableFirstPhrase) {
          continue;
        }

        if (cellVal == 0x00) {
          state.cells[channel][row] = ControlSurfaceState::Cell::FirstPattern;
        } else {
          state.cells[channel][row] = ControlSurfaceState::Cell::Populated;
        }
      }
      data += (songChannelCount - maxChan);
    }
  }

  const int stateMaxChan =
      (SONG_CHANNEL_COUNT < matrixSize) ? SONG_CHANNEL_COUNT : matrixSize;
  for (int channel = 0; channel < stateMaxChan; ++channel) {
    if (mixer_.IsChannelMuted(channel)) {
      for (int row = 0; row < matrixSize; ++row) {
        state.cells[channel][row] = ControlSurfaceState::Cell::Muted;
      }
    }

    const QueueingMode qMode = liveQueueingMode_[channel];
    if (qMode != QM_NONE && qMode != QM_CHAINSTOP) {
      const int row = liveQueuePosition_[channel] - windowOffset;
      if (row >= 0 && row < matrixSize) {
        state.cells[channel][row] = ControlSurfaceState::Cell::Queued;
      }
    }

    if (mixer_.IsChannelPlaying(channel) && viewData_ != nullptr) {
      if (viewData_->currentPlayChain_[channel] != 0xFF) {
        const int row = viewData_->songPlayPos_[channel] - windowOffset;
        if (row >= 0 && row < matrixSize &&
            viewData_->playMode_ != PM_AUDITION) {
          state.cells[channel][row] = ControlSurfaceState::Cell::Playing;
        }
      }
    }
  }
}

unsigned char Player::GetQueuePosition(int i) { return liveQueuePosition_[i]; };

unsigned char Player::GetQueueChainPosition(int i) {
  return liveQueueChainPosition_[i];
};

void Player::QueueChannel(int i, QueueingMode mode, unsigned char position,
                          unsigned char chainpos) {
  liveQueueingMode_[i] = mode;
  liveQueuePosition_[i] = position;
  liveQueueChainPosition_[i] = chainpos;
};

/************************************************************
 Update:
        this one gets called when the audio driver has just sent
        a block of audio and we get room to prepare the next one
 ************************************************************/

PICOTRACKER_FAST_CODE("player_update")
void Player::Update(Observable &o, I_ObservableData *d) {

  // Make sure sync's ok

  MidiService::GetInstance()->Trigger();
  project_->Trigger();

  if (isRunning_) {
    ScopedDWTProfile playerControlProfile(&gPlayerControlProfile,
                                          GetVoicesTotalProfileStats());

    SyncMaster *sync = SyncMaster::GetInstance();
    sync->SetTempo(project_->GetTempo());

    if (!firstPlayCycle_) {
      Groove::GetInstance()->Trigger();
      sync->NextSlice();
      triggerLiveChains_ = false;
      if (retrigAllImmediate_) {
        const int channelCount = SONG_CHANNEL_COUNT;
        for (int i = 0; i < channelCount; i++) {
          QueueChannel(i, QM_TICKSTART, retrigPos_, 0);
        };
        retrigAllImmediate_ = false;
      }
      // Don't advance in audition mode
      if (viewData_->playMode_ != PM_AUDITION)
        moveToNextStep();
      if (triggerLiveChains_) {
        triggerLiveChains();
      };
    }

    {
      const int channelCount = SONG_CHANNEL_COUNT;

      for (int i = 0; i < channelCount; i++) {
        const uint32_t profileChannel = static_cast<uint32_t>(i);
        ScopedDWTProfile channelProfile(
            GetVoiceProfileStats(VoiceProfileComponent::PlayerControl,
                                 profileChannel),
            GetVoiceChannelTotalProfileStats(profileChannel));
        if (timeToStart_[i] > 0) {
          if (--timeToStart_[i] == 0) {
            playCursorPosition(i);
          }
        }
      }

      // Process commands in current phrase
      if (viewData_->playMode_ != PM_AUDITION)
        ProcessCommands(true);

      // Initialise retrigger table
      int32_t instrRetrigger[SONG_CHANNEL_COUNT];
      int32_t noteRetrigger[SONG_CHANNEL_COUNT];
      memset(instrRetrigger, -1, SONG_CHANNEL_COUNT * sizeof(int32_t));
      memset(noteRetrigger, -1, SONG_CHANNEL_COUNT * sizeof(int32_t));

      // Process any table commands now
      for (int channel = 0; channel < channelCount; channel++) {
        const uint32_t profileChannel = static_cast<uint32_t>(channel);
        ScopedDWTProfile channelProfile(
            GetVoiceProfileStats(VoiceProfileComponent::PlayerControl,
                                 profileChannel),
            GetVoiceChannelTotalProfileStats(profileChannel));
        TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
        if (!tpb.GetAutomation()) {
          TablePlayerChange tpc;
          tpc.timeToLive_ = timeToLive_[channel];
          tpc.timeToRelease_ = timeToRelease_[channel];
          tpc.instrRetrigger_ = -1;
          tpc.noteRetrigger_ = -1;
          tpb.ProcessStep(tpc, mixer_.GetChannelNote(channel),
                          lastActiveCommands_[channel]);
          timeToLive_[channel] = tpc.timeToLive_;
          timeToRelease_[channel] = tpc.timeToRelease_;
          instrRetrigger[channel] = tpc.instrRetrigger_;
          noteRetrigger[channel] = tpc.noteRetrigger_;
        }
      }

      // Do we need to kill a voice?
      if (sync->TableSlice()) {
        for (int i = 0; i < channelCount; i++) {
          const uint32_t profileChannel = static_cast<uint32_t>(i);
          ScopedDWTProfile channelProfile(
              GetVoiceProfileStats(VoiceProfileComponent::PlayerControl,
                                   profileChannel),
              GetVoiceChannelTotalProfileStats(profileChannel));
          bool stopped = false;
          if (timeToLive_[i] > 0) {
            if (--timeToLive_[i] == 0) {
              mixer_.StopInstrument(i);
              stopped = true;
            }
          }
          if (!stopped && timeToRelease_[i] > 0) {
            if (--timeToRelease_[i] == 0) {
              mixer_.ReleaseInstrument(i);
            }
          }
          if (!stopped) {
            if (noteRetrigger[i] >= 0) {
              RetriggerChannelInstrumentAtNote(i, noteRetrigger[i], true);
            } else if (instrRetrigger[i] >= 0) {
              RetriggerChannelInstrument(
                  i, DecodeRetriggerOffset(instrRetrigger[i]), true);
            };
          }
        }
      }
    }

    firstPlayCycle_ = false;
    System *system = System::GetInstance();
    now_ = system->GetClock();

    // Notify refresh

    PlayerEvent pe(PET_UPDATE, 0);
    SetChanged();
    NotifyObservers(&pe);
  }
};

/************************************************************
 ProcessCommands:
        Check if there's any command to trigger at current playing
        position for all channels
 ************************************************************/

void Player::ProcessCommands(bool profileChannels) {

  // loop on all channels

  Groove *gs = Groove::GetInstance();

  const int channelCount = SONG_CHANNEL_COUNT;
  for (int i = 0; i < channelCount; i++) {
    const uint32_t profileChannel = static_cast<uint32_t>(i);
    DWTProfileStats *profileStats =
        profileChannels
            ? GetVoiceProfileStats(VoiceProfileComponent::PlayerControl,
                                   profileChannel)
            : nullptr;
    DWTProfileStats *channelTotalStats =
        profileChannels ? GetVoiceChannelTotalProfileStats(profileChannel)
                        : nullptr;
    ScopedDWTProfile channelProfile(profileStats, channelTotalStats);

    if (mixer_.IsChannelPlaying(i)) {

      // check if there's any phrase playing

      uchar phrase = viewData_->currentPlayPhrase_[i];
      if (phrase != 0xFF) {
        if (gs->TriggerChannel(i)) { // If groove says it is time to play
          int pos = viewData_->phrasePlayPos_[i];
          uchar note = viewData_->song_->phrase_.note_[phrase * 16 + pos];
          RowFxResult fx = ResolveRowFx(
              note, viewData_->song_->phrase_.cmd1_[phrase * 16 + pos],
              viewData_->song_->phrase_.param1_[phrase * 16 + pos],
              viewData_->song_->phrase_.cmd2_[phrase * 16 + pos],
              viewData_->song_->phrase_.param2_[phrase * 16 + pos],
              lastActiveCommands_[i], true, false);

          auto dispatchResolvedCommand = [&](bool allowed, TrackerCommand cc,
                                             ushort param) {
            if (!allowed || cc == TrackerCommand::InstrumentCommandNone ||
                cc == TrackerCommand::InstrumentCommandChance ||
                cc == TrackerCommand::InstrumentCommandRandom) {
              return;
            }

            bool handledByPlayer = ProcessChannelCommand(i, cc, param);
            bool executed = handledByPlayer;
            if (!handledByPlayer && !mixer_.ProcessVoiceCommand(i, cc, param)) {
              I_Instrument *instrument = mixer_.GetInstrument(i);
              if (instrument) {
                instrument->ProcessCommand(i, cc, param);
                executed = true;
              }
            } else if (!handledByPlayer) {
              executed = true;
            }
            if (executed) {
              RecordLastActiveCommand(i, cc, param);
            }
          };

          dispatchResolvedCommand(fx.allowCmd1, fx.cmd1, fx.param1);
          dispatchResolvedCommand(fx.allowCmd2, fx.cmd2, fx.param2);
        }
      }
    }
  };
};

bool Player::ProcessChannelCommand(int channel, TrackerCommand cmd,
                                   ushort param) {

  I_Instrument *instr = mixer_.GetInstrument(channel);

  switch (cmd) {
  case TrackerCommand::InstrumentCommandKill:
    if (instr) {
      uint32_t timeToLive = (param & 0xFF);
      timeToLive_[channel] = timeToLive + 1;
    }
    return true;
  case TrackerCommand::InstrumentCommandOff:
    if (instr) {
      uint32_t timeToRelease = (param & 0xFF);
      timeToRelease_[channel] = timeToRelease + 1;
    }
    return true;
  case TrackerCommand::InstrumentCommandTempo: {
    param = std::clamp(param, MIN_TEMPO, MAX_TEMPO);
    Variable *v = project_->FindVariable(FourCC::VarTempo);
    v->SetInt(param);
    SyncMaster *sync = SyncMaster::GetInstance();
    sync->SetTempo(project_->GetTempo());
    return true;
    break;
  }
  case TrackerCommand::InstrumentCommandDelayTime: {
    const int32_t target = std::clamp<int32_t>(
        static_cast<int32_t>(param & 0xFF), DelayOutputEffect::MinimumTime,
        DelayOutputEffect::MaximumTime);
    Variable *variable = project_->FindVariable(FourCC::VarDelayTime);
    NAssert(variable != nullptr);
    if (variable != nullptr) {
      variable->SetInt(target);
    }
    return true;
  }
  case TrackerCommand::InstrumentCommandDelayFeedback: {
    const int32_t target =
        std::clamp<int32_t>(static_cast<int32_t>(param & 0xFF), 0,
                            DelayOutputEffect::MaximumFeedback);
    Variable *variable = project_->FindVariable(FourCC::VarDelayFeedback);
    NAssert(variable != nullptr);
    if (variable != nullptr) {
      variable->SetInt(target);
    }
    return true;
  }
  case TrackerCommand::InstrumentCommandReverbPreDelay:
  case TrackerCommand::InstrumentCommandReverbInputTone:
  case TrackerCommand::InstrumentCommandReverbDiffuse:
  case TrackerCommand::InstrumentCommandReverbDecay:
  case TrackerCommand::InstrumentCommandReverbTailTone:
  case TrackerCommand::InstrumentCommandReverbModulationSpeed:
  case TrackerCommand::InstrumentCommandReverbModulationLevel: {
    const FourCC parameterId = GetReverbCommandParameterId(cmd);
    NAssert(parameterId != FourCC::Default);
    Variable *variable = project_->FindVariable(parameterId);
    NAssert(variable != nullptr);
    if (variable != nullptr) {
      variable->SetInt(static_cast<int32_t>(param & 0xFF));
    }
    return true;
  }
  case TrackerCommand::InstrumentCommandTable: {
    TableHolder *th = TableHolder::GetInstance();
    TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
    param = param & 0x7F;
    Table &table = th->GetTable(param);
    tpb.Start(instr, table, false);
    return true;
    break;
  }
  case TrackerCommand::InstrumentCommandGroove: {
    Groove *gr = Groove::GetInstance();
    bool all = (param & 0xFF00) != 0;
    param = param & 0xFF;
    if (all) {
      const int channelCount = SONG_CHANNEL_COUNT;
      for (int i = 0; i < channelCount; i++) {
        gr->SetGroove(i, param);
      }
    } else {
      gr->SetGroove(channel, param);
    }
    return true;
    break;
  }
  default:
    break;
  };
  return false;
};

void Player::ResetLastActiveCommands() {
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastActiveCommands_[i].lastCmd = TrackerCommand::InstrumentCommandNone;
    lastActiveCommands_[i].lastParam = 0;
    lastActiveCommands_[i].valid = false;
  }
}

void Player::RecordLastActiveCommand(int channel, TrackerCommand cmd,
                                     ushort param) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return;
  }
  UpdateLastActiveCommandState(lastActiveCommands_[channel], cmd, param);
}

/********************************************************
 triggerLiveChains:
        look if there's any chain needed to be started
                i.e. they're queued but on a channel that is not
                yet active
 ********************************************************/

void Player::triggerLiveChains() {

  if (mode_ == PM_LIVE) {
    const int channelCount = SONG_CHANNEL_COUNT;
    for (int i = 0; i < channelCount; i++) {
      if (!(mixer_.IsChannelPlaying(i)) &&
          ((liveQueueingMode_[i] == QM_CHAINSTART) ||
           (liveQueueingMode_[i] == QM_TICKSTART) ||
           (liveQueueingMode_[i] == QM_PHRASESTART))) {
        if (findPlayable(&(liveQueuePosition_[i]), i,
                         liveQueueChainPosition_[i])) {
          mixer_.StartChannel(i);
          updateSongPos(liveQueuePosition_[i], i, liveQueueChainPosition_[i]);
        }
        liveQueueingMode_[i] = QM_NONE;
      }
    }
  }
};

/********************************************************
 updateSongPos:
        sets current song position for a give channel. the
        last parameter allow to start the song position at a
        give chain position directly (for PM_CHAIN/PM_PHRASE)
 ********************************************************/

void Player::updateSongPos(int pos, int channel, int chainPos, int hop) {
  const int channelCount = SONG_CHANNEL_COUNT;
  unsigned char *data = viewData_->song_->data_ + channel + channelCount * pos;
  viewData_->songPlayPos_[channel] = pos;
  viewData_->currentPlayChain_[channel] = *data;
  updateChainPos(chainPos, channel, hop);
};

/********************************************************
 updateChainPos:
        sets current chain position for a give channel
 ********************************************************/

void Player::updateChainPos(int pos, int channel, int hop) {
  unsigned char chain = viewData_->currentPlayChain_[channel];
  if (chain != 0xFF) {
    viewData_->chainPlayPos_[channel] = pos;
    unsigned char *data = viewData_->song_->chain_.data_ + (16 * chain + pos);
    viewData_->currentPlayPhrase_[channel] = *data;
    if (*data == 0xFF) { // This could happen if starting in song mode on a row
                         // where a chain contains no phrase
      mixer_.StopChannel(channel);
    }
  } else {
    viewData_->currentPlayPhrase_[channel] = 0xFF;
    mixer_.StopChannel(channel);
  };
  updatePhrasePos((hop >= 0) ? hop : 0, channel);
}

/********************************************************
 updatePhrasePos:
        sets current phrase position for a give channel.
 ********************************************************/

void Player::updatePhrasePos(int pos, int channel) {

  viewData_->phrasePlayPos_[channel] = pos;

  // See if we need to delay the trigger
  timeToStart_[channel] = 1;

  uchar phrase = viewData_->currentPlayPhrase_[channel];

  // Check both param colum 1 & 2

  TrackerCommand cc = viewData_->song_->phrase_.cmd1_[phrase * 16 + pos];
  if (cc == TrackerCommand::InstrumentCommandDelay) {
    ushort param = viewData_->song_->phrase_.param1_[phrase * 16 + pos];
    timeToStart_[channel] = (param & 0x0F) + 1;
  }

  cc = viewData_->song_->phrase_.cmd2_[phrase * 16 + pos];
  if (cc == TrackerCommand::InstrumentCommandDelay) {
    ushort param = viewData_->song_->phrase_.param2_[phrase * 16 + pos];
    timeToStart_[channel] = (param & 0x0F) + 1;
  }
}

void Player::playCursorPosition(int channel) {

  int pos = viewData_->phrasePlayPos_[channel];

  // Get chain content and see if instr needs to be started/Stopped
  unsigned char currentPhrase = viewData_->currentPlayPhrase_[channel];

  if (currentPhrase != 0xFF) {

    Song *song = viewData_->song_;
    Phrase *phrase = &(song->phrase_);
    unsigned char note = phrase->note_[16 * currentPhrase + pos];
    unsigned char instr = phrase->instr_[16 * currentPhrase + pos];
    RowFxResult fx = ResolveRowFx(note, phrase->cmd1_[16 * currentPhrase + pos],
                                  phrase->param1_[16 * currentPhrase + pos],
                                  phrase->cmd2_[16 * currentPhrase + pos],
                                  phrase->param2_[16 * currentPhrase + pos],
                                  lastActiveCommands_[channel], false);
    note = fx.note;

    TableHolder *th = TableHolder::GetInstance();
    TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
    TablePlayback &atp = TablePlayback::GetAutomationPlayback(channel);

    if (note == NOTE_OFF) {
      mixer_.ReleaseInstrument(channel);
    } else if (note <= HIGHEST_NOTE && fx.allowNote) {
      // clear pending release before next note
      timeToRelease_[channel] = 0;
      InstrumentBank *bank = viewData_->project_->GetInstrumentBank();

      // get instrument for next note

      InstrumentStartMode startMode = InstrumentStartMode::TiedNote;

      I_Instrument *instrument;
      if (instr != 0xFF) {
        instrument = bank->GetInstrument(instr);
        startMode = InstrumentStartMode::NewNote;
      } else {
        instrument = mixer_.GetLastInstrument(channel);
      }

      if (instrument == 0) {
        instrument = bank->GetInstrument(0);
      }

      if (instrument != 0) {

        int chain = viewData_->currentPlayChain_[channel];
        int chainPos = viewData_->chainPlayPos_[channel];
        unsigned char *trsp =
            viewData_->song_->chain_.transpose_ + (16 * chain + chainPos);
        bool preserveNote = false;
        if (instrument->GetType() == IT_SAMPLE) {
          SampleInstrument *sampleInstrument =
              static_cast<SampleInstrument *>(instrument);
          preserveNote = sampleInstrument->HasSlicesForPlayback();
        } else if (instrument->GetType() == IT_SAMPLE_SOURCE) {
          SampleSourceInstrument *sampleSourceInstrument =
              static_cast<SampleSourceInstrument *>(instrument);
          preserveNote = sampleSourceInstrument->HasSlicesForPlayback();
        }
        if (!preserveNote) {
          note += *trsp;
          note += project_->GetTranspose();
        }
        instrumentOnChannel_[channel][0] =
            (instr / 16) > 9 ? 'A' - 10 + (instr / 16) : '0' + (instr / 16);
        instrumentOnChannel_[channel][1] =
            (instr % 16) > 9 ? 'A' - 10 + (instr % 16) : '0' + (instr % 16);
        instrumentOnChannel_[channel][2] = '\0';

        // Check if note is in acceptable midi range

        if (note < 128) {
          if (startMode == InstrumentStartMode::NewNote) {
            mixer_.StopInstrument(channel);
          }
          mixer_.StartInstrument(channel, instrument, note, startMode);
          int instrTable = instrument->GetTable();

          // If an instrument number has been specified && instrument has table,
          // we trigger the table.

          if (instrTable != VAR_OFF &&
              startMode == InstrumentStartMode::NewNote) {
            Table &table = th->GetTable(instrTable);
            bool automated = instrument->GetTableAutomation();
            if (automated) {
              atp.Start(instrument, table, true);
              tpb.Stop();
            } else {
              atp.Stop();
              tpb.Start(instrument, table, false);
            }
          } else {
            // if there was an instrument number, we stop the table
            if (startMode == InstrumentStartMode::NewNote) {
              atp.Stop();
              tpb.Stop();
            }
          }
        } else {
          Trace::Error("Note outside range: %02x", (unsigned int)note);
        }
      }
    }
    if ((note <= HIGHEST_NOTE) || (instr != 0xFF)) {
      I_Instrument *instrument = mixer_.GetInstrument(channel);
      if (instrument) {
        if (instrument->GetTableAutomation()) {
          TablePlayerChange tpc;
          tpc.timeToLive_ = timeToLive_[channel];
          tpc.timeToRelease_ = timeToRelease_[channel];
          tpc.instrRetrigger_ = -1;
          tpc.noteRetrigger_ = -1;
          atp.ProcessStep(tpc, mixer_.GetChannelNote(channel),
                          lastActiveCommands_[channel]);
          timeToLive_[channel] = tpc.timeToLive_;
          timeToRelease_[channel] = tpc.timeToRelease_;
          if (tpc.noteRetrigger_ >= 0) {
            RetriggerChannelInstrumentAtNote(channel, tpc.noteRetrigger_,
                                             false);
          } else if (tpc.instrRetrigger_ >= 0) {
            RetriggerChannelInstrument(
                channel, DecodeRetriggerOffset(tpc.instrRetrigger_), false);
          };
        }
      }
    }
  }
}

void Player::StepAutomationTableForRetrigger(int channel,
                                             I_Instrument *instrument) {
  if ((instrument == 0) || (!instrument->GetTableAutomation())) {
    return;
  }

  int instrTable = instrument->GetTable();
  if (instrTable == VAR_OFF) {
    return;
  }

  TableHolder *th = TableHolder::GetInstance();
  TablePlayback &automationPlayback =
      TablePlayback::GetAutomationPlayback(channel);
  TablePlayerChange tpc;

  Table &table = th->GetTable(instrTable);
  if (automationPlayback.GetTable() != &table) {
    automationPlayback.Start(instrument, table, true);
  }

  tpc.timeToLive_ = timeToLive_[channel];
  tpc.timeToRelease_ = timeToRelease_[channel];
  tpc.instrRetrigger_ = -1;
  tpc.noteRetrigger_ = -1;

  automationPlayback.ProcessStep(tpc, mixer_.GetChannelNote(channel),
                                 lastActiveCommands_[channel]);

  timeToLive_[channel] = tpc.timeToLive_;
  timeToRelease_[channel] = tpc.timeToRelease_;
  // Ignore IRT generated by this follow-up automation step to keep retrigger
  // handling strictly non-recursive.
}

void Player::RetriggerChannelInstrument(int channel, int semitoneOffset,
                                        bool stepAutomationTable) {
  int baseNote = mixer_.GetChannelNote(channel);
  if (baseNote > HIGHEST_NOTE) {
    return;
  }
  RetriggerChannelInstrumentAtNote(channel, baseNote + semitoneOffset,
                                   stepAutomationTable);
}

void Player::RetriggerChannelInstrumentAtNote(int channel, int note,
                                              bool stepAutomationTable) {
  I_Instrument *instrument = mixer_.GetInstrument(channel);

  if (instrument == 0) {
    return;
  }

  while (note > 127) {
    note -= 12;
  }
  while (note < 0) {
    note += 12;
  }
  // clear pending release before stopping
  timeToRelease_[channel] = 0;
  mixer_.StartInstrument(channel, instrument, note,
                         InstrumentStartMode::Retrigger);

  if (stepAutomationTable) {
    StepAutomationTableForRetrigger(channel, instrument);
  }
}

int Player::getChannelHop(int channel, int pos) {

  int phrase = viewData_->currentPlayPhrase_[channel];
  return ResolveRowHop(viewData_->song_->phrase_.cmd1_[phrase * 16 + pos],
                       viewData_->song_->phrase_.param1_[phrase * 16 + pos],
                       viewData_->song_->phrase_.cmd2_[phrase * 16 + pos],
                       viewData_->song_->phrase_.param2_[phrase * 16 + pos]);
}

/********************************************************
 moveToNextStep:
        Atomic routine that triggers the next step for all
        playing channels.
 ********************************************************/

void Player::moveToNextStep() {
  // we'll need to know if any channel is playing

  bool playingChannel = false;
  if (selectionRender_.active &&
      selectionRender_.sourceKind == SelectionResampleSourceKind::Song) {
    selectionRenderSongRowAdvanced_ = false;
  }

  const int channelCount = SONG_CHANNEL_COUNT;
  for (int i = 0; i < channelCount; i++) {
    bool liveTriggered = false;

    switch (liveQueueingMode_[i]) {
    case QM_TICKSTART:
      liveQueueingMode_[i] = QM_NONE;
      if (findPlayable(&(liveQueuePosition_[i]), i,
                       liveQueueChainPosition_[i])) {
        liveTriggered = true;
        updateSongPos(liveQueuePosition_[i], i, liveQueueChainPosition_[i]);
      }
      return;
      break;
    case QM_PHRASESTART:
    case QM_PHRASESTOP:
    case QM_CHAINSTART:
    case QM_CHAINSTOP:
    case QM_NONE:
      break;
    }

    Groove *gs = Groove::GetInstance();

    if (mixer_.IsChannelPlaying(i) && !liveTriggered) {
      playingChannel = true;

      if (gs->TriggerChannel(i)) { // If groove says it is time to play
        if (viewData_->currentPlayPhrase_[i] != 0xFF) {
          int pos = (viewData_->phrasePlayPos_[i]) + 1;
          if (shouldStopSelectionRenderAtPhraseRow(i, pos)) {
            stopLocked(PSM_RELEASE);
            return;
          }
          if (pos != 16) {
            int hop = getChannelHop(i, pos);
            if (hop >= 0) {
              if (mode_ != PM_PHRASE) {
                moveToNextPhrase(i, hop);
              } else {
                updatePhrasePos(hop, i);
              }
            } else {
              updatePhrasePos(pos, i);
            }
          } else { // HOP.. something should be done so that if
                   // next chain has a hop on pos zero, it is effective
            if (mode_ != PM_PHRASE) {
              moveToNextPhrase(i);
            } else {
              updatePhrasePos(0, i);
            }
          }
        }
      }
    }
  }
  if (selectionRender_.active &&
      selectionRender_.sourceKind == SelectionResampleSourceKind::Song &&
      selectionRenderSongRowAdvanced_) {
    if (selectionRender_.currentSongRow < selectionRender_.endSongRow) {
      selectionRender_.currentSongRow++;
    } else {
      stopLocked(PSM_RELEASE);
      return;
    }
  }

  // if no channel is playing we allow straight
  // queueing of chains
  if (!playingChannel) {
    triggerLiveChains_ = true;
  };
}

/********************************************************
 moveToNextPhrase:
        Compute what is the next phrase to be triggered on
        the selected channel. Called when the current phrase
        has reached the end
 ********************************************************/

void Player::moveToNextPhrase(int channel, int hop) {

  // First check if we're in live mode and the channel
  // has been trigged in immediate mode. In which case we
  // do the action straight away (START/STOP)

  if (mode_ == PM_LIVE) {

    switch (liveQueueingMode_[channel]) {
    case QM_TICKSTART:
    case QM_PHRASESTART:
      if (findPlayable(&(liveQueuePosition_[channel]), channel,
                       liveQueueChainPosition_[channel])) {
        updateSongPos(liveQueuePosition_[channel], channel,
                      liveQueueChainPosition_[channel], hop);
      }
      liveQueueingMode_[channel] = QM_NONE;
      return;
      break;
    case QM_PHRASESTOP:
      mixer_.StopChannel(channel);
      liveQueueingMode_[channel] = QM_NONE;
      return;
    case QM_CHAINSTART:
    case QM_CHAINSTOP:
    case QM_NONE:
      break;
    }
  }

  // If nothing has been triggered, we need to find what is
  // The next phrase to play

  int chain = viewData_->currentPlayChain_[channel];
  int pos = (viewData_->chainPlayPos_[channel]) + 1;
  if (shouldStopSelectionRenderAtChainRow(channel, pos)) {
    stopLocked(PSM_RELEASE);
    return;
  }

  // Look if there' any data at current position
  // which means we continue in the current chain

  bool canContinue = (pos < 16);
  if (canContinue) {
    unsigned char *data = viewData_->song_->chain_.data_ + (16 * chain + pos);
    canContinue = (*data != 0xFF);
  }

  // If so, we trigger it. Otherwise, we go to the next phrase

  if (canContinue) {
    updateChainPos(pos, channel, hop);
  } else { // Should move to next chain
    if (selectionRender_.active &&
        selectionRender_.sourceKind == SelectionResampleSourceKind::Chain &&
        channel == selectionRender_.trackerChannel) {
      stopLocked(PSM_RELEASE);
      return;
    }
    if ((mode_ == PM_SONG) || (mode_ == PM_LIVE)) {
      moveToNextChain(channel, hop); // HOP. here
    } else {
      updateChainPos(0, channel, hop);
    };
  }
}

/********************************************************
 moveToNextChain:
        Compute what is the next chain to be triggered on
        the selected channel. Called when the current chain
        has reached the end.
 ********************************************************/

void Player::moveToNextChain(int channel, int hop) {

  // if there's unplaying channels queue they should be started
  // once all position have been updated

  triggerLiveChains_ = true;

  bool searchNext = true;
  int nextPos = 0;
  int chainPosition = 0;

  // Hop here ?

  // See if current channel has been queued to play something
  // in normal mode

  if (mode_ == PM_LIVE) {
    switch (liveQueueingMode_[channel]) {

    case QM_CHAINSTART:
    case QM_PHRASESTART:

      if (findPlayable(&(liveQueuePosition_[channel]), channel,
                       liveQueueChainPosition_[channel])) {
        nextPos = liveQueuePosition_[channel];
        searchNext = false;
        liveQueueingMode_[channel] = QM_NONE;
        chainPosition = liveQueueChainPosition_[channel];
      } else {
        liveQueueingMode_[channel] = QM_NONE;
      }
      break;

    case QM_CHAINSTOP:
    case QM_PHRASESTOP:
      mixer_.StopChannel(channel);
      liveQueueingMode_[channel] = QM_NONE;
      return;

    case QM_TICKSTART:
    case QM_NONE:
      break;
    }
  }

  // if live mode didn't queue anything, we find the next to play

  if (searchNext) {
    const int channelCount = SONG_CHANNEL_COUNT;
    int pos = (viewData_->songPlayPos_[channel]) + 1;
    if (selectionRender_.active &&
        selectionRender_.sourceKind == SelectionResampleSourceKind::Song) {
      if (selectionRender_.currentSongRow >= selectionRender_.endSongRow) {
        stopLocked(PSM_RELEASE);
        return;
      }
      selectionRenderSongRowAdvanced_ = true;
    }

    bool loopBack = pos >= SONG_ROW_COUNT;
    unsigned char *data = nullptr;
    if (!loopBack) {
      data = viewData_->song_->data_ + channel + channelCount * pos;
      loopBack = (*data == 0xFF);
      // Check if first step of chain contains somethin, if not we loop back
      if (!loopBack) {
        unsigned char step = viewData_->song_->chain_.data_[*data * 16];
        loopBack = (step == 0xFF);
      };
    }
    if (ShouldWrapSongLoop(songLoopRange_, pos, loopBack)) {
      pos = songLoopRange_.startRow;
      loopBack = false;
    }
    if (loopBack) {
      if (stopAtEnd_) {
        // If stopAtEnd is enabled and we need to loop back, stop playback
        // instead.
        // Use the same graceful stop path as the public sequencer stop, but
        // stay in the locked context to avoid deadlocking the audio thread.
        stopLocked(PSM_RELEASE);
        return;
      }

      // Otherwise proceed with normal loop back behavior
      if (data == nullptr) {
        pos = SONG_ROW_COUNT - 1;
        data = viewData_->song_->data_ + channel + channelCount * pos;
      } else {
        data -= channelCount;
        pos--;
      }
      while (pos >= 0) {
        if (*data == 0xFF) { // we stop searching if there's a blank
          break;
        } else { // Or if first phrase of chain is empty
          if (viewData_->song_->chain_.data_[(*data) * 16] == 0xFF) {
            break;
          }
        }
        if (pos != 0)
          data -= channelCount;
        pos--;
      };
      pos++;
    }
    nextPos = pos;
  }
  // Do a last check in case we had only one chain and it go destroyed

  if (isPlayable(nextPos, channel, chainPosition)) {
    updateSongPos(nextPos, channel, chainPosition, hop);
  } else {
    mixer_.StopChannel(channel);
  }
};

double Player::GetPlayTime() {
  AudioOut *out = mixer_.GetAudioOut();
  double currentTime = out->GetStreamTime();
  return currentTime - startTime_;
};

int Player::GetPlayedBufferPercentage() {
  unsigned int beatCount = SyncMaster::GetInstance()->GetBeatCount();
  if (beatCount != lastBeatCount_) {
    lastBeatCount_ = beatCount;
    lastPercentage_ = mixer_.GetPlayedBufferPercentage();
  }
  return lastPercentage_;
};

PlayerEvent::PlayerEvent(PlayerEventType type, unsigned int tickCount)
    : ViewEvent(VET_PLAYER_POSITION_UPDATE) {
  type_ = type;
  tickCount_ = tickCount;
}

PlayerEventType PlayerEvent::GetType() { return type_; };

unsigned int PlayerEvent::GetTickCount() { return tickCount_; };

void Player::StartStreaming(const char *name, int startSample) {
  mixer_.StartStreaming(name, startSample);
}

void Player::StartLoopingStreaming(const char *name) {
  mixer_.StartLoopingStreaming(name);
}

void Player::StopStreaming() { mixer_.StopStreaming(); }

bool Player::IsStreamingActive() { return mixer_.IsStreamingActive(); }

void Player::StartRecordStreaming(uint16_t *srcBuffer, uint32_t size,
                                  bool stereo) {
  mixer_.StartRecordStreaming(srcBuffer, size, stereo);
}

void Player::StopRecordStreaming() { mixer_.StopRecordStreaming(); }

bool Player::IsPlaying() { return mixer_.IsPlaying(); }

bool Player::IsAuditioning() const {
  return (isRunning_ && mode_ == PM_AUDITION) ||
         directNoteAuditionState_.IsActive();
}

bool Player::IsMidiInputPlaying() const {
  return midiInputNoteState_.IsActive();
}

bool Player::IsMidiInputChannelPlaying(uint8_t channel) const {
  return midiInputNoteState_.IsChannelActive(channel);
}

bool Player::IsInstrumentInUse(const I_Instrument *instrument) {
  if (instrument == nullptr) {
    return false;
  }

  for (uint8_t channel = 0; channel < SONG_CHANNEL_COUNT; channel++) {
    if (mixer_.GetInstrument(channel) == instrument) {
      return true;
    }
  }
  return false;
}

bool Player::IsTailPlaying() {
  return !isRunning_ && !directNoteAuditionState_.IsActive() &&
         !midiInputNoteState_.IsActive() && mixer_.HasTail();
}

etl::string<STRING_AUDIO_API_MAX> Player::GetAudioAPI() {
  AudioOut *out = mixer_.GetAudioOut();
  return (out) ? out->GetAudioAPI() : "";
};

etl::string<STRING_AUDIO_DEVICE_MAX> Player::GetAudioDevice() {
  AudioOut *out = mixer_.GetAudioOut();
  return (out) ? out->GetAudioDevice() : "";
};

int Player::GetAudioBufferSize() {
  AudioOut *out = mixer_.GetAudioOut();
  return (out) ? out->GetAudioBufferSize() : 0;
};

int Player::GetAudioRequestedBufferSize() {
  AudioOut *out = mixer_.GetAudioOut();
  return (out) ? out->GetAudioRequestedBufferSize() : 0;
};

int Player::GetAudioPreBufferCount() {
  AudioOut *out = mixer_.GetAudioOut();
  return (out) ? out->GetAudioPreBufferCount() : 0;
};

etl::array<stereosample, SONG_CHANNEL_COUNT> *Player::GetMixerLevels() {
  return mixer_.GetMixerLevels();
}

// Direct note playback methods

void Player::PlayNote(unsigned short instrumentIndex, unsigned short channel,
                      unsigned char note, unsigned char velocity) {
  PlayDirectNote(instrumentIndex, channel, note, velocity,
                 directNoteAuditionState_);
}

void Player::PlayDirectNote(unsigned short instrumentIndex,
                            unsigned short channel, unsigned char note,
                            unsigned char velocity,
                            DirectNoteChannelState &state) {
  if (!project_ || instrumentIndex >= MAX_INSTRUMENT_COUNT ||
      channel >= SONG_CHANNEL_COUNT)
    return;

  InstrumentBank *bank = project_->GetInstrumentBank();
  if (!bank)
    return;

  I_Instrument *instrument = bank->GetInstrument(instrumentIndex);
  if (instrument && !instrument->IsEmpty()) {
    // Use the channel modulo SONG_CHANNEL_COUNT to ensure it's within range
    int playerChannel = channel % SONG_CHANNEL_COUNT;
    // clear pending release before new note
    timeToRelease_[playerChannel] = 0;
    hex2char(static_cast<uint8_t>(instrumentIndex),
             instrumentOnChannel_[playerChannel]);
    mixer_.StartInstrument(playerChannel, instrument, note,
                           InstrumentStartMode::NewNote, velocity);
    state.StartChannel(static_cast<uint8_t>(playerChannel));
  }
}

void Player::StopNote(unsigned short instrumentIndex, unsigned short channel) {
  (void)instrumentIndex;
  StopDirectNote(channel, directNoteAuditionState_);
}

void Player::StopDirectNote(unsigned short channel,
                            DirectNoteChannelState &state) {
  if (channel >= SONG_CHANNEL_COUNT) {
    return;
  }
  // Use the channel modulo SONG_CHANNEL_COUNT to ensure it's within range
  int playerChannel = channel % SONG_CHANNEL_COUNT;
  mixer_.StopInstrument(playerChannel);
  state.StopChannel(static_cast<uint8_t>(playerChannel));
}

void Player::PlayMidiNote(MidiInputSource inputSource, uint8_t midiChannel,
                          uint8_t instrumentIndex, uint8_t note,
                          uint8_t velocity) {
  if (!project_ || midiChannel >= MIDI_INPUT_CHANNEL_COUNT ||
      instrumentIndex >= MAX_INSTRUMENT_COUNT || note > 127 || velocity == 0) {
    return;
  }

  InstrumentBank *bank = project_->GetInstrumentBank();
  I_Instrument *instrument =
      bank ? bank->GetInstrument(instrumentIndex) : nullptr;
  if (!instrument || instrument->IsEmpty()) {
    return;
  }

  const int songChannel = project_->song_.FindAvailableMidiInputSongChannel(
      midiChannel, midiNoteTracker_.getActiveChannelMask());
  if (songChannel < 0) {
    return;
  }

  if (midiNoteTracker_.registerNote(inputSource, note, midiChannel,
                                    static_cast<uint8_t>(songChannel),
                                    velocity)) {
    PlayDirectNote(instrumentIndex, static_cast<uint16_t>(songChannel), note,
                   velocity, midiInputNoteState_);
    SetChanged();
    PlayerEvent pe(PET_NOTE_UPDATE);
    NotifyObservers(&pe);
  }
}

void Player::StopMidiNote(MidiInputSource inputSource, uint8_t midiChannel,
                          uint8_t note) {
  if (midiChannel >= MIDI_INPUT_CHANNEL_COUNT || note > 127) {
    return;
  }

  const int songChannel =
      midiNoteTracker_.unregisterNote(inputSource, note, midiChannel);
  if (songChannel >= 0) {
    // MIDI note-off is a musical release. Hard stops are reserved for panic,
    // project changes, and MIDI input assignment changes.
    mixer_.ReleaseInstrument(songChannel);
    midiInputNoteState_.StopChannel(static_cast<uint8_t>(songChannel));
    SetChanged();
    PlayerEvent pe(PET_NOTE_UPDATE);
    NotifyObservers(&pe);
  }
}

void Player::ReleaseMidiInputSource(MidiInputSource inputSource) {
  mixer_.Lock();
  const uint16_t releasedChannelMask =
      midiNoteTracker_.clearInputSource(inputSource);
  if (releasedChannelMask == 0U) {
    mixer_.Unlock();
    return;
  }

  for (uint8_t songChannel = 0; songChannel < SONG_CHANNEL_COUNT;
       songChannel++) {
    if ((releasedChannelMask & (1U << songChannel)) != 0U) {
      // Losing an input is equivalent to receiving note-off for every note it
      // owns. Preserve each instrument's release envelope.
      mixer_.ReleaseInstrument(songChannel);
      midiInputNoteState_.StopChannel(songChannel);
    }
  }
  mixer_.Unlock();

  SetChanged();
  PlayerEvent pe(PET_NOTE_UPDATE);
  NotifyObservers(&pe);
}

void Player::OnMidiInputAssignmentChanged(uint8_t songChannel) {
  if (songChannel >= SONG_CHANNEL_COUNT) {
    return;
  }

  mixer_.Lock();
  mixer_.StopChannel(songChannel);
  midiNoteTracker_.clearChannel(songChannel);
  directNoteAuditionState_.StopChannel(songChannel);
  midiInputNoteState_.StopChannel(songChannel);
  liveQueueingMode_[songChannel] = QM_NONE;
  timeToLive_[songChannel] = 0;
  timeToRelease_[songChannel] = 0;
  timeToStart_[songChannel] = 0;
  TablePlayback::GetTablePlayback(songChannel).Stop();
  TablePlayback::GetAutomationPlayback(songChannel).Stop();
  if (viewData_) {
    viewData_->currentPlayChain_[songChannel] = EMPTY_SONG_VALUE;
    viewData_->currentPlayPhrase_[songChannel] = EMPTY_SONG_VALUE;
  }
  mixer_.Unlock();
}
