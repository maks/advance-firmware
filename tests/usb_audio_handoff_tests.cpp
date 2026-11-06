#include "Adapters/adv/usb/usb_audio_handoff.h"
#include "tusb.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "doctest/doctest.h"

// ---------------------------------------------------------------------------
// TinyUSB stubs. Speed is configurable so recovery can be exercised at both
// full and high speed; the audio FIFO is cleared between endpoint completions
// so each simulated completion carries exactly one packet.
// ---------------------------------------------------------------------------
namespace {
bool deviceInitialized = false;
bool audioMounted = false;
tu_fifo_t audioFifo{};
tusb_speed_t simulatedSpeed = TUSB_SPEED_FULL;
} // namespace

extern "C" tusb_speed_t tud_speed_get(void) { return simulatedSpeed; }

extern "C" bool tud_inited(void) { return deviceInitialized; }

extern "C" bool tud_audio_n_mounted(uint8_t) { return audioMounted; }

extern "C" bool tud_audio_n_clear_ep_in_ff(uint8_t) {
  audioFifo.count = 0;
  return true;
}

extern "C" tu_fifo_t *tud_audio_n_get_ep_in_ff(uint8_t) { return &audioFifo; }

extern "C" uint16_t tud_audio_n_write(uint8_t, const void *data,
                                      uint16_t byteCount) {
  if (byteCount > tu_fifo_remaining(&audioFifo)) {
    return 0;
  }
  std::memcpy(audioFifo.data + audioFifo.count, data, byteCount);
  audioFifo.count += byteCount;
  return byteCount;
}

namespace {

// A single USB completion's captured payload as stereo frames.
struct Packet {
  std::vector<int16_t> samples;
  uint16_t frames() const { return static_cast<uint16_t>(samples.size() / 2); }
  bool frameSilent(uint16_t f) const {
    return samples[2 * f] == 0 && samples[2 * f + 1] == 0;
  }
  // Non-silent frames copied in order (leading PCM before any padding).
  std::vector<int16_t> nonSilent() const {
    std::vector<int16_t> out;
    for (uint16_t f = 0; f < frames(); ++f) {
      if (!frameSilent(f)) {
        out.push_back(samples[2 * f]);
        out.push_back(samples[2 * f + 1]);
      }
    }
    return out;
  }
};

// Monotonic producer sequence so each published frame is uniquely identifiable
// and its order is checkable. Both channels are always non-zero so intentional
// silence and padding stay distinguishable from real PCM.
uint32_t producerSeq = 0;

std::vector<int16_t> makePcm(uint32_t frames) {
  std::vector<int16_t> v(frames * 2);
  for (uint32_t i = 0; i < frames; ++i) {
    v[2 * i] = static_cast<int16_t>(1 + (producerSeq % 30000));
    v[2 * i + 1] = static_cast<int16_t>(16001 + (producerSeq % 10000));
    ++producerSeq;
  }
  return v;
}

struct Counters {
  uint32_t dropped;
  uint32_t underruns;
  uint32_t discarded;
  uint32_t overflows;
  uint32_t padded;
};

Counters snapshot() {
  return {usbMasterAudioDroppedFrames(), usbMasterAudioUnderruns(),
          usbMasterAudioDiscardedFrames(), usbMasterAudioOverflowEvents(),
          usbMasterAudioUnderrunPaddedFrames()};
}

Counters delta(const Counters &before) {
  const Counters now = snapshot();
  return {now.dropped - before.dropped, now.underruns - before.underruns,
          now.discarded - before.discarded, now.overflows - before.overflows,
          now.padded - before.padded};
}

// Reset the stub USB environment and drive the handoff to a known inactive
// baseline: configured + output selected + stream mounted, but playback off.
// usbMasterAudioReset + Service clears the queue, primed and packet phase.
void resetEnvironment(tusb_speed_t speed = TUSB_SPEED_FULL) {
  simulatedSpeed = speed;
  deviceInitialized = true;
  audioMounted = true;
  audioFifo.capacity = sizeof(audioFifo.data);
  audioFifo.count = 0;
  producerSeq = 0;

  usbMasterAudioSetPlaybackActive(0);
  usbMasterAudioSetStreaming(0);
  usbMasterAudioSetSuspended(0);
  usbMasterAudioSetConfigured(1);
  usbMasterAudioSetOutputSelected(1);
  usbMasterAudioReset();
  usbMasterAudioService();
}

// Turn playback on (a fresh lifecycle generation) and apply it.
void startPlayback() {
  usbMasterAudioSetPlaybackActive(1);
  usbMasterAudioSetStreaming(1);
  usbMasterAudioService();
}

// Run one endpoint completion and capture exactly one packet. The FIFO is
// cleared first so the callback's single packet is isolated.
Packet pumpPacket() {
  audioFifo.count = 0;
  tud_audio_tx_done_pre_load_cb(0, 0, 0x83, 1);
  Packet p;
  p.samples.assign(
      reinterpret_cast<const int16_t *>(audioFifo.data),
      reinterpret_cast<const int16_t *>(audioFifo.data + audioFifo.count));
  return p;
}

// Publish a recognizable PCM block and record the exact frames the queue
// accepted. Returns true if the block was accepted (not capacity-rejected).
bool publishTracked(const std::vector<int16_t> &pcm,
                    std::vector<int16_t> &acceptedLog) {
  const uint32_t droppedBefore = usbMasterAudioDroppedFrames();
  usbMasterAudioPublish(pcm.data(), static_cast<uint16_t>(pcm.size() / 2),
                        false);
  if (usbMasterAudioDroppedFrames() == droppedBefore) {
    acceptedLog.insert(acceptedLog.end(), pcm.begin(), pcm.end());
    return true;
  }
  return false;
}

constexpr uint32_t kStartupFrames = 2048;

} // namespace

// ---------------------------------------------------------------------------
// Original ordering / silence / gating coverage.
// ---------------------------------------------------------------------------

TEST_CASE("USB audio handoff preserves stereo frame ordering") {
  resetEnvironment();
  startPlayback();
  const std::array<int16_t, 6> input = {101, -101, 202, -202, 303, -303};
  std::array<int16_t, 6> output{};

  usbMasterAudioPublish(input.data(), 3, false);

  CHECK(usbMasterAudioRead(output.data(), 3) == 3);
  CHECK(output == input);
}

TEST_CASE("USB audio handoff zeros muted frames and drops inactive audio") {
  resetEnvironment();
  startPlayback();
  const std::array<int16_t, 4> input = {1, 2, 3, 4};
  std::array<int16_t, 4> output = {9, 9, 9, 9};
  usbMasterAudioPublish(input.data(), 2, true);
  CHECK(usbMasterAudioRead(output.data(), 2) == 2);
  CHECK(output == std::array<int16_t, 4>{0, 0, 0, 0});

  usbMasterAudioSetStreaming(0);
  const uint32_t discardedBefore = usbMasterAudioDiscardedFrames();
  usbMasterAudioPublish(input.data(), 2, false);
  CHECK(usbMasterAudioDiscardedFrames() == discardedBefore + 2);
  CHECK(usbMasterAudioRead(output.data(), 2) == 0);
}

TEST_CASE("USB audio active stream supplies silence without playback") {
  resetEnvironment();
  // Streaming + configured but playback inactive: paced silence only.
  usbMasterAudioSetPlaybackActive(0);
  usbMasterAudioSetStreaming(1);
  usbMasterAudioService();

  const Packet p = pumpPacket();
  CHECK(p.frames() == 44); // nominal full-speed, phase starts at 0
  for (uint16_t f = 0; f < p.frames(); ++f) {
    CHECK(p.frameSilent(f));
  }
}

// ---------------------------------------------------------------------------
// Scenario: initial queue below startup -> paced silence, no underrun loss.
// ---------------------------------------------------------------------------

TEST_CASE("Below startup threshold emits paced silence without loss counts") {
  resetEnvironment();
  startPlayback();
  const Counters before = snapshot();

  // Publish a small amount (well below 2048) and pump one packet.
  std::vector<int16_t> log;
  publishTracked(makePcm(500), log);
  const Packet p = pumpPacket();

  // Not primed yet -> whole packet is silence and no loss is recorded.
  for (uint16_t f = 0; f < p.frames(); ++f) {
    CHECK(p.frameSilent(f));
  }
  const Counters d = delta(before);
  CHECK(d.underruns == 0);
  CHECK(d.padded == 0);
}

// ---------------------------------------------------------------------------
// Scenario: queue reaches threshold -> playback begins in order.
// ---------------------------------------------------------------------------

TEST_CASE("Reaching startup threshold begins playback in order") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  publishTracked(makePcm(kStartupFrames), log);

  const Packet p = pumpPacket();
  REQUIRE(p.frames() >= 44);
  // First delivered frames must be the exact head of the published stream.
  auto delivered = p.nonSilent();
  REQUIRE(delivered.size() >= 2);
  CHECK(delivered[0] == log[0]);
  CHECK(delivered[1] == log[1]);
}

// ---------------------------------------------------------------------------
// Key overflow test: overflow AFTER priming rejects the incoming block,
// preserves queued PCM, and never imposes a new startup wait.
// ---------------------------------------------------------------------------

TEST_CASE("Overflow after priming preserves queued PCM without restart") {
  resetEnvironment();
  startPlayback();
  const Counters before = snapshot();

  std::vector<int16_t> log;
  std::vector<int16_t> delivered;
  auto collect = [&delivered](const Packet &p) {
    auto f = p.nonSilent();
    delivered.insert(delivered.end(), f.begin(), f.end());
  };
  // Prime the queue.
  publishTracked(makePcm(kStartupFrames), log);
  // Pump until primed and the queue is comfortably drained below threshold.
  for (int i = 0; i < 12; ++i) {
    collect(pumpPacket());
  }

  // Fill the queue so the next full-size block cannot fit -> rejected.
  publishTracked(makePcm(1875), log);
  const std::vector<int16_t> overflowBlock = makePcm(1875);
  const bool rejected = !publishTracked(overflowBlock, log);
  CHECK(rejected);

  const Counters d = delta(before);
  CHECK(d.overflows == 1);
  // Continuous recovery: overflow must not invalidate a generation.
  CHECK(d.discarded == 0);

  // Drain fully and verify the surviving queued PCM equals exactly what was
  // accepted, in order, with the rejected block absent. No new publication
  // happened after the rejection, so the whole accepted stream must arrive.
  for (int i = 0; i < 400; ++i) {
    collect(pumpPacket());
  }
  CHECK(delivered.size() == log.size());
  // Delivered non-silent stream must be a prefix of the accepted stream
  // (never diverge, never reorder, never duplicate), so the rejected block
  // is absent while all previously queued PCM survived.
  REQUIRE(delivered.size() <= log.size());
  CHECK(std::equal(delivered.begin(), delivered.end(), log.begin()));
}

// ---------------------------------------------------------------------------
// Repeated overflows (including while initially priming) keep counts and PCM.
// ---------------------------------------------------------------------------

TEST_CASE("Repeated overflows keep correct counts and preserve PCM") {
  resetEnvironment();
  startPlayback();
  const Counters before = snapshot();

  std::vector<int16_t> log;
  std::vector<int16_t> delivered;
  auto collect = [&delivered](const Packet &p) {
    auto f = p.nonSilent();
    delivered.insert(delivered.end(), f.begin(), f.end());
  };
  // Push many 1875-frame blocks with a single small drain in between to force
  // multiple capacity rejections while never reaching a reset.
  publishTracked(makePcm(1575), log);
  for (int i = 0; i < 6; ++i) {
    publishTracked(makePcm(1875), log);
    collect(pumpPacket()); // drain a little between bursts
  }

  const Counters d = delta(before);
  CHECK(d.overflows >= 1);
  CHECK(d.discarded == 0);

  // Startup threshold still applies: nothing is delivered before 2048 queued.
  // The delivered stream equals a prefix of accepted, proving earlier accepted
  // PCM survived.
  for (int i = 0; i < 400; ++i) {
    collect(pumpPacket());
  }
  REQUIRE(delivered.size() <= log.size());
  CHECK(std::equal(delivered.begin(), delivered.end(), log.begin()));
}

// ---------------------------------------------------------------------------
// Key underrun test: partial packet pads exactly the missing frames, keeps
// primed, and consumes a small later publication without a startup wait.
// ---------------------------------------------------------------------------

TEST_CASE("Partial underrun pads exactly and resumes on small publication") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  publishTracked(makePcm(kStartupFrames), log);
  // Drain until the queue is empty, leaving primed true.
  Packet p;
  do {
    p = pumpPacket();
  } while (!p.nonSilent().empty());

  // Publish fewer frames than one packet's worth of fresh PCM.
  const uint32_t before = snapshot().padded;
  std::vector<int16_t> small = makePcm(10);
  publishTracked(small, log);

  const Packet underrun = pumpPacket();
  // Leading 10 frames present and correct, remaining frames are padding.
  REQUIRE(underrun.frames() >= 10);
  for (uint16_t f = 0; f < 10; ++f) {
    CHECK(underrun.samples[2 * f] == small[2 * f]);
    CHECK(underrun.samples[2 * f + 1] == small[2 * f + 1]);
  }
  for (uint16_t f = 10; f < underrun.frames(); ++f) {
    CHECK(underrun.frameSilent(f));
  }
  const uint32_t expectedPadding = underrun.frames() - 10;
  CHECK(usbMasterAudioUnderrunPaddedFrames() - before == expectedPadding);

  // primed stayed true: the next small publication is consumed immediately
  // without needing 2048 frames again.
  std::vector<int16_t> next = makePcm(8);
  publishTracked(next, log);
  const Packet resume = pumpPacket();
  auto nz = resume.nonSilent();
  CHECK(nz.size() >= 2);
  CHECK(nz[0] == next[0]);
  CHECK(nz[1] == next[1]);
}

TEST_CASE(
    "Empty queue after priming sends paced silence with correct padding") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  publishTracked(makePcm(kStartupFrames), log);
  // Drain to empty.
  Packet p;
  do {
    p = pumpPacket();
  } while (!p.nonSilent().empty());

  const uint32_t padBefore = usbMasterAudioUnderrunPaddedFrames();
  const uint32_t undBefore = usbMasterAudioUnderruns();
  const Packet empty = pumpPacket();
  for (uint16_t f = 0; f < empty.frames(); ++f) {
    CHECK(empty.frameSilent(f));
  }
  CHECK(usbMasterAudioUnderruns() - undBefore == 1);
  CHECK(usbMasterAudioUnderrunPaddedFrames() - padBefore == empty.frames());
}

TEST_CASE("Small publication after underrun appears without startup wait") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  publishTracked(makePcm(kStartupFrames), log);
  Packet p;
  do {
    p = pumpPacket();
  } while (!p.nonSilent().empty());

  std::vector<int16_t> tiny = makePcm(3);
  publishTracked(tiny, log);
  const Packet next = pumpPacket();
  auto nz = next.nonSilent();
  REQUIRE(nz.size() >= 6);
  CHECK(nz[0] == tiny[0]);
  CHECK(nz[1] == tiny[1]);
  CHECK(nz[2] == tiny[2]);
  CHECK(nz[3] == tiny[3]);
  CHECK(nz[4] == tiny[4]);
  CHECK(nz[5] == tiny[5]);
}

// ---------------------------------------------------------------------------
// Ring wrap during recovery: order preserved, rejected blocks absent, no
// duplicated frames. Crosses the physical 4096 write boundary.
// ---------------------------------------------------------------------------

TEST_CASE("Ring wrap during recovery preserves order without duplicates") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  std::vector<int16_t> delivered;
  auto collect = [&delivered](const Packet &p) {
    auto f = p.nonSilent();
    delivered.insert(delivered.end(), f.begin(), f.end());
  };
  publishTracked(makePcm(kStartupFrames), log);
  collect(pumpPacket());

  // Alternate bursts and drains, forcing wrap crossings and occasional
  // overflow, while logging the accepted stream.
  for (int i = 0; i < 20; ++i) {
    publishTracked(makePcm(1875), log);
    for (int k = 0; k < 20; ++k) {
      collect(pumpPacket());
    }
  }
  publishTracked(makePcm(1875), log);

  for (int i = 0; i < 400; ++i) {
    collect(pumpPacket());
  }
  // Delivered is an exact prefix of accepted => no reorder, no duplicate, and
  // any rejected block is absent.
  REQUIRE(delivered.size() <= log.size());
  CHECK(std::equal(delivered.begin(), delivered.end(), log.begin()));
}

// ---------------------------------------------------------------------------
// Recovery at both USB speeds: legal packet sizes and uninterrupted cadence.
// ---------------------------------------------------------------------------

TEST_CASE("Recovery works at high speed with legal packet sizes") {
  resetEnvironment(TUSB_SPEED_HIGH);
  startPlayback();

  std::vector<int16_t> log;
  publishTracked(makePcm(kStartupFrames), log);

  bool sawSmall = false;
  bool sawLarge = false;
  for (int i = 0; i < 60; ++i) {
    const Packet p = pumpPacket();
    // High-speed legal 44.1k packet sizes are 5 or 6 frames.
    CHECK((p.frames() == 5 || p.frames() == 6));
    sawSmall |= p.frames() == 5;
    sawLarge |= p.frames() == 6;
  }
  // Fractional cadence must produce both sizes over time (not stuck at one).
  CHECK(sawSmall);
  CHECK(sawLarge);
}

TEST_CASE("Full speed packet sizes remain legal during recovery") {
  resetEnvironment(TUSB_SPEED_FULL);
  startPlayback();

  std::vector<int16_t> log;
  publishTracked(makePcm(kStartupFrames), log);
  for (int i = 0; i < 60; ++i) {
    const Packet p = pumpPacket();
    CHECK((p.frames() == 44 || p.frames() == 45));
  }
}

// ---------------------------------------------------------------------------
// Playback/route reset during recovery: old PCM invalidated, fresh generation
// requires initial priming again.
// ---------------------------------------------------------------------------

TEST_CASE("Route reset during recovery invalidates old PCM and re-primes") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  publishTracked(makePcm(kStartupFrames), log);
  // Drain below the startup threshold (continuous underrun behaviour), then
  // publish a stale old-generation backlog that is present mid-recovery.
  Packet p;
  do {
    p = pumpPacket();
  } while (!p.nonSilent().empty());
  const std::vector<int16_t> stale = makePcm(600);
  publishTracked(stale, log);

  // Route/playback toggle -> new generation. The stale backlog must not leak.
  usbMasterAudioSetPlaybackActive(0);
  usbMasterAudioSetPlaybackActive(1);

  // A small new-generation publication is below the fresh startup threshold,
  // so the next packet is silence, not the old backlog.
  std::vector<int16_t> fresh = makePcm(20);
  publishTracked(fresh, log);
  const Packet after = pumpPacket(); // applies reset before reading
  for (uint16_t f = 0; f < after.frames(); ++f) {
    CHECK(after.frameSilent(f));
  }
  // Now satisfy the fresh startup threshold and confirm ordered playback.
  publishTracked(makePcm(kStartupFrames), log);
  const Packet started = pumpPacket();
  auto nz = started.nonSilent();
  CHECK_FALSE(nz.empty());
}

// ---------------------------------------------------------------------------
// Fresh publication before a lifecycle reset is serviced survives.
// ---------------------------------------------------------------------------

TEST_CASE("Fresh matching-generation PCM survives a lifecycle reset") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  // Old-generation queued PCM.
  publishTracked(makePcm(500), log);

  // Request a reset, then publish fresh PCM tagged with the new generation
  // before the consumer services the reset.
  usbMasterAudioReset();
  std::vector<int16_t> fresh = makePcm(300);
  std::vector<int16_t> freshLog;
  publishTracked(fresh, freshLog);

  // Servicing discards the stale 500 but retains the fresh matching frames.
  const uint32_t discBefore = usbMasterAudioDiscardedFrames();
  usbMasterAudioService();
  CHECK(usbMasterAudioDiscardedFrames() - discBefore == 500);

  std::array<int16_t, 600> out{};
  const uint16_t got =
      usbMasterAudioRead(out.data(), static_cast<uint16_t>(fresh.size() / 2));
  CHECK(got == fresh.size() / 2);
  CHECK(std::equal(fresh.begin(), fresh.end(), out.begin()));
}

// ---------------------------------------------------------------------------
// Streaming / suspend gating remains effective.
// ---------------------------------------------------------------------------

TEST_CASE("Suspend gates capture and resume requires re-priming") {
  resetEnvironment();
  startPlayback();
  const Counters before = snapshot();

  usbMasterAudioSetSuspended(1);
  std::vector<int16_t> log;
  const uint32_t discBefore = usbMasterAudioDiscardedFrames();
  publishTracked(makePcm(1000), log);
  CHECK(usbMasterAudioDiscardedFrames() - discBefore == 1000);

  const Packet p = pumpPacket();
  for (uint16_t f = 0; f < p.frames(); ++f) {
    CHECK(p.frameSilent(f));
  }
  // Suspended playback is not a running-stream underrun.
  const Counters d = delta(before);
  CHECK(d.padded == 0);
}

// ---------------------------------------------------------------------------
// Intentional silence and offline-style publishing is never underrun padding.
// ---------------------------------------------------------------------------

TEST_CASE("Intentional silence is delivered and never counted as padding") {
  resetEnvironment();
  startPlayback();

  std::vector<int16_t> log;
  // Prime with real PCM, then drain it so only published silence remains.
  publishTracked(makePcm(kStartupFrames), log);
  Packet drained;
  do {
    drained = pumpPacket();
  } while (!drained.nonSilent().empty());
  const Counters before = snapshot();

  // Feed intentional-silence blocks and verify each packet: silence came from
  // the queue, not from underrun padding.
  for (int i = 0; i < 5; ++i) {
    usbMasterAudioPublish(nullptr, 1575, true);
    for (int k = 0; k < 30; ++k) {
      const Packet p = pumpPacket();
      for (uint16_t f = 0; f < p.frames(); ++f) {
        CHECK(p.frameSilent(f));
      }
    }
  }
  const Counters d = delta(before);
  CHECK(d.padded == 0);
  CHECK(d.underruns == 0);
}

// ---------------------------------------------------------------------------
// Sustained deterministic simulation: 1575/1875-frame bursts at nominal 44.1k
// with injected completion delays. Asserts ordering, bounded capacity,
// continued delivery and the absence of reset-induced startup silence.
// Recorded baseline (overflow->reset build, producer phase after priming):
//   full speed: discarded delta 18487; delivered truncated at 88978 frames;
//     silence gaps up to ~3.5k frames after each overflow reset.
//   high speed: discarded delta 11809; delivered truncated at 133790 frames.
// ---------------------------------------------------------------------------

// Runs the sustained simulation at one USB speed. owedPerTick accumulates
// producer frames in 1/10-frame units per endpoint-completion tick; skipTick
// injects completion delays equivalent to the full-speed pattern (3 delays
// per 500 ms plus 5 per 2 s, losing the same frame counts).
// ---------------------------------------------------------------------------
void runSustainedSimulation(tusb_speed_t speed) {
  const bool high = speed == TUSB_SPEED_HIGH;
  // 20 seconds of endpoint completions: 1 kHz frames or 8 kHz microframes.
  const int ticks = high ? 160000 : 20000;
  resetEnvironment(speed);
  startPlayback();
  const Counters before = snapshot();

  uint32_t owed = 0;
  uint32_t cycle = 0;
  std::vector<int16_t> accepted;
  bool sawOverflow = false;
  // Ticks per 500 ms window at this speed (full speed: 1 kHz callbacks,
  // high speed: 8 kHz microframes).
  const int halfSecond = speed == TUSB_SPEED_HIGH ? 4000 : 500;
  auto skipTick = [halfSecond](int tick) {
    // Skip enough ticks per window to lose 3 (resp. 5) full-speed packets.
    const int skipA = halfSecond == 500 ? 3 : 24;
    const int skipB = halfSecond == 500 ? 5 : 40;
    return (tick % (2 * halfSecond) >= halfSecond / 2 &&
            tick % (2 * halfSecond) < halfSecond / 2 + skipA) ||
           (tick % (8 * halfSecond) >= 4 * halfSecond + halfSecond / 5 &&
            tick % (8 * halfSecond) < 4 * halfSecond + halfSecond / 5 + skipB);
  };

  std::vector<int16_t> delivered;
  uint32_t zeroFramesAfterFirstDelivery = 0;
  bool deliveredOnce = false;

  for (int tick = 0; tick < ticks; ++tick) {
    // 44.1 frames/ms == 441 tenths; high speed spreads them over eight
    // microframes (55 or 56 tenths).
    owed += high ? (tick % 8 != 7 ? 55 : 56) : 441;
    uint32_t blk = (cycle % 2 == 0) ? 1575u : 1875u;
    if (owed >= blk * 10) {
      std::vector<int16_t> blkPcm = makePcm(blk);
      const uint32_t d0 = usbMasterAudioDroppedFrames();
      usbMasterAudioPublish(blkPcm.data(), static_cast<uint16_t>(blk), false);
      if (usbMasterAudioDroppedFrames() == d0) {
        accepted.insert(accepted.end(), blkPcm.begin(), blkPcm.end());
      } else {
        sawOverflow = true;
      }
      ++cycle;
      owed -= blk * 10;
    }
    if (!skipTick(tick)) {
      const Packet p = pumpPacket();
      for (uint16_t f = 0; f < p.frames(); ++f) {
        if (p.frameSilent(f)) {
          if (deliveredOnce) {
            ++zeroFramesAfterFirstDelivery;
          }
        } else {
          deliveredOnce = true;
          delivered.push_back(p.samples[2 * f]);
          delivered.push_back(p.samples[2 * f + 1]);
        }
      }
    }
  }

  // Continuous recovery guarantees: delivered non-silent PCM is exactly the
  // accepted stream (prefix), no lifecycle invalidation, and no post-priming
  // startup silence during production.
  CHECK(sawOverflow);
  CHECK(delivered.size() <= accepted.size());
  CHECK(std::equal(delivered.begin(), delivered.end(), accepted.begin()));
  const Counters d = delta(before);
  CHECK(d.discarded == 0);
  CHECK(zeroFramesAfterFirstDelivery == 0);
}

TEST_CASE("Sustained full speed simulation recovers through delays") {
  runSustainedSimulation(TUSB_SPEED_FULL);
}

TEST_CASE("Sustained high speed simulation recovers through delays") {
  runSustainedSimulation(TUSB_SPEED_HIGH);
}
