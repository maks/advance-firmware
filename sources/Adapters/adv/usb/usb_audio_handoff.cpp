#include "usb_audio_handoff.h"
#include "config/MemorySections.h"
#include "tusb.h"

#include <atomic>
#include <cstring>

namespace {

constexpr uint32_t kQueueFrameCapacity = 4096;
constexpr uint32_t kQueueFrameMask = kQueueFrameCapacity - 1;

static_assert((kQueueFrameCapacity & kQueueFrameMask) == 0);

struct StereoFrame {
  int16_t left;
  int16_t right;
};

// These 32 KiB of payload/tag storage use spare internal D2 SRAM rather than
// the crowded AXI RAM used by .bss. The producer initializes samples and tags
// before publishing writeFrame; NOLOAD contents are never read at startup.
PICOTRACKER_D2_BUFFER StereoFrame queue[kQueueFrameCapacity];
// Preserve fresh PCM published after a transition, even when the audio task
// runs before the USB consumer can apply the reset. Tags share the SPSC
// publication fence with their samples and need no atomic writes of their own.
PICOTRACKER_D2_BUFFER uint32_t frameGeneration[kQueueFrameCapacity];
std::atomic<uint32_t> writeFrame{0};
std::atomic<uint32_t> readFrame{0};
std::atomic<uint32_t> droppedFrames{0};
std::atomic<uint32_t> underrunCount{0};
std::atomic<uint32_t> discardedFrames{0};
// Counts whole blocks rejected because the queue lacked capacity. Overflow is
// a local loss, not a lifecycle event, so it never changes the generation.
std::atomic<uint32_t> overflowEvents{0};
// Frames of a running stream's packet that had no PCM available and went out
// as the zero-initialization. Excludes startup priming and lifecycle gaps.
std::atomic<uint32_t> underrunPaddedFrames{0};
constexpr uint32_t streamFlag = 1;
constexpr uint32_t playbackFlag = 2;
constexpr uint32_t outputFlag = 4;
constexpr uint32_t suspendedFlag = 8;
constexpr uint32_t configuredFlag = 16;
constexpr uint32_t generationStep = 32;
// Flags and generation change atomically, so a task cannot acknowledge a new
// generation while still observing the preceding route/playback state.
std::atomic<uint32_t> requestedGeneration{0};
std::atomic<uint32_t> acceptedGeneration{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free);
bool primed = false;
uint32_t packetPhase = 0;
constexpr uint32_t startupFrames = 2048;
// Mixer blocks are published in bursts of up to 1875 frames. Keep the
// handoff target low enough that one complete block plus normal occupancy
// variation cannot fill the 4096-frame queue.
constexpr uint32_t clockTargetFrames = 2048;

uint32_t queuedFrames() {
  return writeFrame.load(std::memory_order_acquire) -
         readFrame.load(std::memory_order_relaxed);
}

bool canPublish() {
  const uint32_t state = requestedGeneration.load(std::memory_order_acquire);
  constexpr uint32_t required =
      streamFlag | playbackFlag | outputFlag | configuredFlag;
  return (state & (required | suspendedFlag)) == required;
}

void changeState(uint32_t flag, bool value) {
  uint32_t previous = requestedGeneration.load(std::memory_order_acquire);
  while (((previous & flag) != 0) != value) {
    const uint32_t next =
        ((previous + generationStep) & ~flag) | (value ? flag : 0);
    if (requestedGeneration.compare_exchange_weak(previous, next,
                                                  std::memory_order_acq_rel)) {
      return;
    }
  }
}

} // namespace

void usbMasterAudioPublish(const int16_t *interleavedStereo,
                           uint16_t frameCount, bool silence) {
  const uint32_t generation =
      requestedGeneration.load(std::memory_order_acquire);
  if (!canPublish()) {
    discardedFrames.fetch_add(frameCount, std::memory_order_relaxed);
    return;
  }
  if (frameCount == 0 || frameCount > kQueueFrameCapacity) {
    droppedFrames.fetch_add(frameCount, std::memory_order_relaxed);
    return;
  }

  const uint32_t write = writeFrame.load(std::memory_order_relaxed);
  const uint32_t read = readFrame.load(std::memory_order_acquire);
  const uint32_t available = kQueueFrameCapacity - (write - read);
  if (frameCount > available) {
    // Reject the incoming block only. Queued PCM, positions, generation,
    // packet phase and startup state are untouched, so the stream keeps
    // playing the backlog instead of restarting with a fresh startup wait.
    droppedFrames.fetch_add(frameCount, std::memory_order_relaxed);
    overflowEvents.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  const uint32_t firstFrame = write & kQueueFrameMask;
  const uint32_t firstCount = frameCount < kQueueFrameCapacity - firstFrame
                                  ? frameCount
                                  : kQueueFrameCapacity - firstFrame;
  const uint32_t secondCount = frameCount - firstCount;
  if (silence) {
    std::memset(queue + firstFrame, 0, firstCount * sizeof(StereoFrame));
    if (secondCount != 0) {
      std::memset(queue, 0, secondCount * sizeof(StereoFrame));
    }
  } else {
    std::memcpy(queue + firstFrame, interleavedStereo,
                firstCount * sizeof(StereoFrame));
    if (secondCount != 0) {
      std::memcpy(queue, interleavedStereo + firstCount * 2,
                  secondCount * sizeof(StereoFrame));
    }
  }
  for (uint32_t frame = 0; frame < frameCount; ++frame) {
    frameGeneration[(write + frame) & kQueueFrameMask] = generation;
  }
  if (generation != requestedGeneration.load(std::memory_order_acquire)) {
    discardedFrames.fetch_add(frameCount, std::memory_order_relaxed);
    return;
  }
  writeFrame.store(write + frameCount, std::memory_order_release);
}

uint16_t usbMasterAudioRead(int16_t *interleavedStereo,
                            uint16_t maxFrameCount) {
  usbMasterAudioService();
  const uint32_t read = readFrame.load(std::memory_order_relaxed);
  const uint32_t write = writeFrame.load(std::memory_order_acquire);
  const uint32_t frameCount =
      (write - read) < maxFrameCount ? (write - read) : maxFrameCount;
  if (frameCount == 0) {
    return 0;
  }

  const uint32_t firstFrame = read & kQueueFrameMask;
  const uint32_t firstCount = frameCount < kQueueFrameCapacity - firstFrame
                                  ? frameCount
                                  : kQueueFrameCapacity - firstFrame;
  const uint32_t secondCount = frameCount - firstCount;
  std::memcpy(interleavedStereo, queue + firstFrame,
              firstCount * sizeof(StereoFrame));
  if (secondCount != 0) {
    std::memcpy(interleavedStereo + firstCount * 2, queue,
                secondCount * sizeof(StereoFrame));
  }
  readFrame.store(read + frameCount, std::memory_order_release);
  return static_cast<uint16_t>(frameCount);
}

void usbMasterAudioClearQueue() {
  const uint32_t write = writeFrame.load(std::memory_order_acquire);
  const uint32_t read = readFrame.load(std::memory_order_relaxed);
  discardedFrames.fetch_add(write - read, std::memory_order_relaxed);
  readFrame.store(write, std::memory_order_release);
}

void usbMasterAudioReset() {
  // This may be requested from an ISR. Never touch TinyUSB or ring read state
  // here; the USB task applies the reset before preparing the next packet.
  requestedGeneration.fetch_add(generationStep, std::memory_order_release);
}

uint32_t usbMasterAudioDroppedFrames() {
  return droppedFrames.load(std::memory_order_relaxed);
}

uint32_t usbMasterAudioUnderruns() {
  return underrunCount.load(std::memory_order_relaxed);
}

uint32_t usbMasterAudioDiscardedFrames() {
  return discardedFrames.load(std::memory_order_relaxed);
}

uint32_t usbMasterAudioOverflowEvents() {
  return overflowEvents.load(std::memory_order_relaxed);
}

uint32_t usbMasterAudioUnderrunPaddedFrames() {
  return underrunPaddedFrames.load(std::memory_order_relaxed);
}

void usbMasterAudioSetStreaming(uint8_t active) {
  changeState(streamFlag, active != 0);
}

bool usbMasterAudioIsStreaming() {
  const uint32_t state = requestedGeneration.load(std::memory_order_acquire);
  return (state & (streamFlag | configuredFlag | suspendedFlag)) ==
         (streamFlag | configuredFlag);
}

void usbMasterAudioSetOutputSelected(uint8_t selected) {
  changeState(outputFlag, selected != 0);
}

bool usbMasterAudioOutputSelected() {
  return (requestedGeneration.load(std::memory_order_acquire) & outputFlag) !=
         0;
}

void usbMasterAudioSetConfigured(uint8_t active) {
  changeState(configuredFlag, active != 0);
}

void usbMasterAudioSetSuspended(uint8_t active) {
  changeState(suspendedFlag, active != 0);
}

void usbMasterAudioSetPlaybackActive(uint8_t active) {
  changeState(playbackFlag, active != 0);
}

void usbMasterAudioService() {
  const uint32_t generation =
      requestedGeneration.load(std::memory_order_acquire);
  if (generation == acceptedGeneration.load(std::memory_order_relaxed)) {
    return;
  }
  const uint32_t read = readFrame.load(std::memory_order_relaxed);
  const uint32_t write = writeFrame.load(std::memory_order_acquire);
  uint32_t next = read;
  while (next != write &&
         frameGeneration[next & kQueueFrameMask] != generation) {
    ++next;
  }
  if (generation != requestedGeneration.load(std::memory_order_acquire)) {
    return;
  }
  discardedFrames.fetch_add(next - read, std::memory_order_relaxed);
  readFrame.store(next, std::memory_order_release);
  if (tud_inited() && tud_audio_n_mounted(0)) {
    (void)tud_audio_n_clear_ep_in_ff(0);
  }
  primed = false;
  packetPhase = 0;
  acceptedGeneration.store(generation, std::memory_order_release);
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t functionId,
                                   uint8_t endpoint, uint8_t alternateSetting) {
  (void)rhport;
  (void)endpoint;
  if (functionId != 0 || alternateSetting != 1) {
    return true;
  }
  // TinyUSB invokes this before set_itf_cb on the first transfer. Establish
  // the new stream here so its very first packet uses freshly reset state.
  const uint32_t state = requestedGeneration.load(std::memory_order_acquire);
  if ((state & (configuredFlag | suspendedFlag)) == configuredFlag) {
    usbMasterAudioSetStreaming(1);
  }
  usbMasterAudioService();

  const uint32_t intervals = tud_speed_get() == TUSB_SPEED_HIGH ? 8000 : 1000;
  const uint16_t smallPacketFrames = 44100 / intervals;
  const uint32_t phaseIncrement = 44100 % intervals;
  uint16_t packetFrames = smallPacketFrames;

  const uint32_t generation =
      acceptedGeneration.load(std::memory_order_acquire);
  const bool playing = canPublish();
  const uint32_t available = queuedFrames();
  if (playing && !primed && available >= startupFrames) {
    primed = true;
  }
  if (playing && primed) {
    // Keep the exact 44.1 kHz fractional packet cadence, then make a small
    // rate correction from the full handoff occupancy. Choosing a 45-frame
    // packet on every interval while above target would consume 45,000 frames
    // per second and repeatedly drain the queue into an audible underrun.
    const int32_t queueError = static_cast<int32_t>(available) -
                               static_cast<int32_t>(clockTargetFrames);
    const int32_t phaseCorrection =
        queueError / 64 > 32 ? 32
                             : (queueError / 64 < -32 ? -32 : queueError / 64);
    int32_t correctedPhase = static_cast<int32_t>(packetPhase) +
                             static_cast<int32_t>(phaseIncrement) +
                             phaseCorrection;
    if (correctedPhase >= static_cast<int32_t>(intervals)) {
      correctedPhase -= static_cast<int32_t>(intervals);
      ++packetFrames;
    }
    packetPhase = static_cast<uint32_t>(correctedPhase);
  } else {
    // Continue the nominal cadence for silence packets too; stream state
    // transitions reset the phase before audio resumes.
    packetPhase += phaseIncrement;
    if (packetPhase >= intervals) {
      packetPhase -= intervals;
      ++packetFrames;
    }
  }

  int16_t packet[45 * 2]{};
  if (playing && primed) {
    const uint16_t copied = usbMasterAudioRead(packet, packetFrames);
    if (copied != packetFrames) {
      // Pad only the missing frames with the zero initialization and stay
      // running: primed remains true and the packet phase keeps advancing,
      // so PCM published on the next audio block is delivered immediately.
      underrunCount.fetch_add(1, std::memory_order_relaxed);
      underrunPaddedFrames.fetch_add(packetFrames - copied,
                                     std::memory_order_relaxed);
    }
  }
  if (generation != requestedGeneration.load(std::memory_order_acquire)) {
    // A route or playback change while copying must not leak old PCM.
    std::memset(packet, 0, sizeof(packet));
    usbMasterAudioService();
  }
  // One packet per endpoint completion. Empty playback produces a paced
  // silence packet, never enough synthetic data to fill the entire FIFO.
  (void)tud_audio_n_clear_ep_in_ff(0);
  const uint16_t bytes = packetFrames * 4;
  const uint16_t written = tud_audio_n_write(0, packet, bytes);
  if (written != bytes) {
    droppedFrames.fetch_add(packetFrames, std::memory_order_relaxed);
  }
  return true;
}
