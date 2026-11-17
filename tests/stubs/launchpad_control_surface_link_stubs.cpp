/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Application/AppWindow.h"
#include "Application/Audio/AudioFileStreamer.h"
#include "Application/Audio/RecordStreamer.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Player/Player.h"
#include "Application/Player/PlayerMixer.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Views/BaseClasses/View.h"
#include "Application/Views/BaseClasses/ViewEvent.h"
#include "Application/Views/ViewData.h"
#include "Services/Midi/ControlSurface/ApcMiniMk2Surface.h"
#include "Services/Midi/MidiService.h"

namespace {
int gPlayerSongStartButtonCallCount = 0;
int gPlayerPlayNoteCallCount = 0;
int gPlayerStopNoteCallCount = 0;
SequencerMode gSequencerMode = SM_SONG;
} // namespace

void ResetLaunchpadControlSurfaceTestState() {
  gPlayerSongStartButtonCallCount = 0;
  gPlayerPlayNoteCallCount = 0;
  gPlayerStopNoteCallCount = 0;
  gSequencerMode = SM_SONG;
}

void SetPlayerSequencerModeForTest(SequencerMode mode) {
  gSequencerMode = mode;
}

int GetPlayerSongStartButtonCallCount() {
  return gPlayerSongStartButtonCallCount;
}

int GetPlayerPlayNoteCallCount() { return gPlayerPlayNoteCallCount; }

int GetPlayerStopNoteCallCount() { return gPlayerStopNoteCallCount; }

MidiService::MidiService() {}

MidiService::~MidiService() {}

void MidiService::Update(Observable &o, I_ObservableData *d) {
  (void)o;
  (void)d;
}

void MidiService::registerControlSurfaceInputs(IControlSurface &surface) {
  (void)surface;
}

void MidiService::SendControlSurfaceRawImmediate(const uint8_t *data,
                                                 uint8_t len) {
  (void)data;
  (void)len;
}

ApcMiniMk2Surface::ApcMiniMk2Surface() : ControlSurfaceDevice(8) {}

ApcMiniMk2Surface::~ApcMiniMk2Surface() {}

bool ApcMiniMk2Surface::Init(MidiService &midiService) {
  return ControlSurfaceDevice::Init(midiService);
}

bool ApcMiniMk2Surface::OnMidiMessage(MidiMessage &message) {
  (void)message;
  return false;
}

void ApcMiniMk2Surface::RefreshLeds() {}

void ApcMiniMk2Surface::OnViewDataChanged(I_ObservableData *data) {
  (void)data;
}

void ApcMiniMk2Surface::OnSurfaceStopped() {}

Player::Player()
    : Observable(&observers_), viewData_(nullptr), project_(nullptr),
      sequencerMode_(SM_SONG), isRunning_(false), stopAtEnd_(false) {}

Player *Player::GetExistingInstance() {
  if (!etl::singleton<Player>::is_valid()) {
    return nullptr;
  }
  return &etl::singleton<Player>::instance();
}

void Player::Update(Observable &o, I_ObservableData *d) {
  (void)o;
  (void)d;
}

SequencerMode Player::GetSequencerMode() { return gSequencerMode; }

bool Player::IsRunning() { return isRunning_; }

void Player::Stop(PlayerStopMode stopMode) {
  (void)stopMode;
  isRunning_ = false;
}

bool Player::ToggleLiveStartStop(MixerServiceMode msmMode) {
  (void)msmMode;
  gPlayerSongStartButtonCallCount++;
  return true;
}

void Player::OnSongStartButton(unsigned int from, unsigned int to,
                               bool requestStop, bool forceImmediate,
                               MixerServiceMode msmMode, bool stopAtEnd,
                               int useStartRow, SongLoopRange songLoopRange) {
  (void)from;
  (void)to;
  (void)requestStop;
  (void)forceImmediate;
  (void)msmMode;
  (void)stopAtEnd;
  (void)useStartRow;
  (void)songLoopRange;
  gPlayerSongStartButtonCallCount++;
  isRunning_ = true;
}

void Player::PlayNote(unsigned short instrumentIndex, unsigned short channel,
                      unsigned char note, unsigned char velocity) {
  (void)instrumentIndex;
  (void)channel;
  (void)note;
  (void)velocity;
  gPlayerPlayNoteCallCount++;
}

void Player::StopNote(unsigned short instrumentIndex, unsigned short channel) {
  (void)instrumentIndex;
  (void)channel;
  gPlayerStopNoteCallCount++;
}

void Player::PlayMidiNote(MidiInputSource inputSource, uint8_t midiChannel,
                          uint8_t instrumentIndex, uint8_t note,
                          uint8_t velocity) {
  (void)inputSource;
  (void)midiChannel;
  (void)instrumentIndex;
  (void)note;
  (void)velocity;
  gPlayerPlayNoteCallCount++;
}

void Player::ApplyMidiVoiceCC(MidiInputSource, uint8_t, uint8_t, uint8_t,
                              uint8_t) {}

void Player::ResetMidiVoicePickup(MidiInputSource, uint8_t) {}

void Player::StopMidiNote(MidiInputSource inputSource, uint8_t midiChannel,
                          uint8_t note) {
  (void)inputSource;
  (void)midiChannel;
  (void)note;
  gPlayerStopNoteCallCount++;
}

void Player::ReleaseMidiInputSource(MidiInputSource inputSource) {
  (void)inputSource;
}

void Player::OnMidiInputAssignmentChanged(uint8_t songChannel) {
  (void)songChannel;
}

bool Player::IsChannelPlaying(int channel) {
  (void)channel;
  return false;
}

int Player::GetChannelSongPlayPosition(int channel) {
  (void)channel;
  return 0;
}

QueueingMode Player::GetQueueingMode(int i) {
  (void)i;
  return QM_NONE;
}

unsigned char Player::GetQueuePosition(int i) {
  (void)i;
  return 0;
}

void Player::SetChannelMute(int channel, bool mute) {
  (void)channel;
  (void)mute;
}

bool Player::IsChannelMuted(int channel) {
  (void)channel;
  return false;
}

void Player::FillControlSurfaceState(ControlSurfaceState &state,
                                     int windowOffset, int matrixSize) {
  (void)windowOffset;
  (void)matrixSize;
  state.isRunning = false;
  state.isLiveMode = (gSequencerMode == SM_LIVE);

  for (auto &column : state.cells) {
    for (auto &cell : column) {
      cell = ControlSurfaceState::Cell::Empty;
    }
  }
}

GUIColor AppWindow::GetColor(ColorDefinition cd) {
  (void)cd;
  return GUIColor(0, 0, 0);
}

AudioFileStreamer::AudioFileStreamer() : project_(nullptr) {}

AudioFileStreamer::~AudioFileStreamer() {}

bool AudioFileStreamer::Render(AudioSample *buffer, int samplecount) {
  (void)buffer;
  (void)samplecount;
  return false;
}

RecordStreamer::RecordStreamer()
    : srcBuffer_(nullptr), bufferSize_(0), stereo_(false), mode_(RSM_STOPPED),
      srcBufferPos_(0) {}

RecordStreamer::~RecordStreamer() {}

bool RecordStreamer::Render(AudioSample *buffer, int samplecount) {
  (void)buffer;
  (void)samplecount;
  return false;
}

WavFile::WavFile()
    : readBufferSize_(0), samples_(nullptr), sampleBufferSize_(0), size_(0),
      sampleRate_(0), channelCount_(0), bytePerSample_(0), audioFormat_(0),
      dataPosition_(0), readCount_(0) {}

etl::expected<void, WAVEFILE_ERROR> WavFile::Open(const char *path) {
  (void)path;
  return {};
}

bool WavFile::IsOpen() const { return false; }

void *WavFile::GetSampleBuffer(int note) {
  (void)note;
  return nullptr;
}

void WavFile::SetSampleBuffer(short *ptr) { samples_ = ptr; }

int WavFile::GetSize(int note) {
  (void)note;
  return 0;
}

int WavFile::GetSampleRate(int note) {
  (void)note;
  return 0;
}

int WavFile::GetChannelCount(int note) {
  (void)note;
  return 0;
}

int WavFile::GetRootNote(int note) {
  (void)note;
  return 0;
}

bool WavFile::GetBuffer(long start, long sampleCount) {
  (void)start;
  (void)sampleCount;
  return false;
}

float WavFile::GetLengthInSec() { return 0.0f; }

uint32_t WavFile::GetDiskSize(int note) {
  (void)note;
  return 0;
}

bool WavFile::Rewind() { return false; }

bool WavFile::Read(void *buff, uint32_t btr, uint32_t *bytesRead) {
  (void)buff;
  (void)btr;
  if (bytesRead != nullptr) {
    *bytesRead = 0;
  }
  return false;
}

bool WavFile::ReadFloat(float *buff, uint32_t maxSamples,
                        uint32_t *samplesRead) {
  (void)buff;
  (void)maxSamples;
  if (samplesRead != nullptr) {
    *samplesRead = 0;
  }
  return false;
}

void WavFile::Close() {}

long WavFile::readBlock(long position, long count) {
  (void)position;
  (void)count;
  return 0;
}

PlayerMixer::PlayerMixer() : Observable() {}

void PlayerMixer::Update(Observable &o, I_ObservableData *d) {
  (void)o;
  (void)d;
}

PlayerEvent::PlayerEvent(PlayerEventType type, unsigned int tickCount)
    : ViewEvent(VET_UPDATE), type_(type), tickCount_(tickCount) {}

PlayerEventType PlayerEvent::GetType() { return type_; }

unsigned int PlayerEvent::GetTickCount() { return tickCount_; }

ViewEvent::ViewEvent(ViewEventType type, void *data)
    : type_(type), data_(data) {}

ViewEventType ViewEvent::GetType() { return type_; }

void *ViewEvent::GetData() { return data_; }

ViewData::ViewData(Project *project)
    : Observable(&observers_), project_(project), song_(nullptr), songX_(0),
      songY_(0), songOffset_(0), chainRow_(0), chainCol_(0), currentChain_(0),
      currentPhrase_(0), currentInstrumentID_(0), currentTable_(0),
      currentGroove_(0), currentFXSlot_(0), playMode_(PM_SONG),
      phraseCurPos_(0) {}

ViewData::~ViewData() {}

uint32_t ViewData::GetSongOffset() const {
  return static_cast<uint32_t>(songOffset_);
}

void ViewData::SetSongOffset(uint32_t offset) { songOffset_ = offset; }

void ViewData::RefreshSongViewport() {}

int View::songRowCount_ = 16;

SyncMaster::SyncMaster() {}

bool SyncMaster::MajorSlice() { return false; }
