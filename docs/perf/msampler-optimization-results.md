# MSAMPLER optimization results

Measured on the same connected Advance and Basic Piano eight-note test project
as the original capture, with Release optimization and symbols. The user confirmed
that playback UI response improved and project saving succeeded during playback.
Raw optimized snapshots and interval summary are in
[msampler-eight-note-optimized.json](msampler-eight-note-optimized.json).
The earlier [findings](msampler-findings.md) describe the original bottlenecks.

| Measurement | Original | Optimized |
| --- | ---: | ---: |
| Mean audio preparation | 9.13 ms | 4.89 ms |
| Mean stream request service | 39.45 ms | 9.57 ms |
| Mean UI event dispatch | 3.55 ms | 1.34 ms |
| Output underrun increment | 3 | 0 |

The original interval was nominally 30 seconds; the optimized interval measured
30.17 seconds between snapshots. Scopes include preemption and blocking, so these
are elapsed timings, not exclusive CPU measurements. Original underruns may
include debugger effects; they are not a clean spontaneous-underrun baseline.

Optimized capture details:

- 1661 audio blocks; 5.15 ms maximum preparation time in this capture.
- 544 completed stream requests; 14.80 ms maximum service time.
- Queue waiting averaged 77.45 ms and peaked at 153.99 ms. The lifetime queue
  watermark reached 16; no enqueue failed during this capture. The queued
  prefetch reserve matters: service time alone is not a latency guarantee.
- Zero page misses, interpolation misses, output underruns, stream errors,
  queue overflows, SD errors, or SD timeouts during the interval.
- 442 file cache hits; 103 opens and 273 seeks. A stream file cache has eight
  slots; changing active assets can still evict files.
- 36,274 sectors in 6866 DMA reads: 5.28 sectors per DMA on average, including
  metadata/single-sector reads. Unaligned reads are batched through 4 KiB of
  AXI SRAM rather than one sector per transfer.
- 76 sampled button events; mean queue delay 0.043 ms, maximum 0.823 ms.
  Maximum event creation through dispatch completion was 0.826 ms. GPIO scans
  were at most 16.44 ms apart. These exclude panel scanout and do not measure
  an individual physical press before the GPIO scan.
- Maximum UI dispatch was 11.60 ms; maximum gap between UI events was 37.88 ms.

## Implementation

SD DMA completion blocks on a static IRQ semaphore under an I/O mutex. Card-ready
polls yield one RTOS tick between attempts. SD boots before FreeRTOS starts, so
boot retains its original polling/IRQ behavior; the first scheduler-context I/O
creates synchronization objects and sets the interrupt to the kernel-safe priority.
Reads and writes to unaligned/non-DMA buffers use the shared eight-sector staging
buffer. Cache maintenance protects DMA-visible data.

The streaming worker retains at most eight LRU file handles, skips seeks when the
file is already at the requested offset, closes idle handles, and closes the cache
when an instrument arena retires. FatFs's fixed file pool has twelve slots, leaving
four for project/UI work. The renderer resolves contiguous sample spans at block
and span boundaries instead of scanning atomic page state twice per sample. A
missing page now counts a miss and fades/ends that voice instead of freezing its
cursor silently. Button GPIO sampling increased from 20 Hz to 100 Hz.

Perf counters now expose queue delay/depth, file-open/seek/read timings, DMA
batching/errors, page misses, input scan spacing, and sampled input queue/dispatch
latency. Epoch-based maxima isolate each capture from startup/loading maxima.

## Debugger correction

A first capture using target-side compare-sections corrupted the Audio task's
stack. OpenOCD's default work area is the first 64 KiB of DTCM, overlapping that
stack. The capture script now compares the flash build CRC/length header using
memory reads only. Both VS Code launch configurations enable work-area backup for
other debugger operations. After reset, the corrected capture completed normally.

## Validation and limits

- Main host suite: 548 tests passed, including sample-accurate mono/stereo playback
  across attack/page boundaries at 0.5x, 1x and 4x, and failed-stream fade/retirement.
- Advance Release, Advance Debug, and WASM Release builds passed.
- The optimized Release was flashed and restarted; the user confirmed improved
  UI response and a successful project save while playing.
- Release AXI SRAM: 506,080 / 524,288 bytes; Debug: 506,208 / 524,288 bytes.
  DTCM remains 126,336 / 131,072 bytes. D3: 27,096 / 65,536 bytes. The DMA staging
  buffer uses 4096 bytes of AXI SRAM; file-cache storage and synchronization
  controls are in explicitly initialized D3 storage. No new DTCM buffers.

This short capture validates this workload. It does not qualify all cards,
fragmentation, maximum pitch ratios, or a sustained 30-minute eight-voice run.
In particular, the measured 154 ms queue delay must be considered when qualifying
higher pitch speeds, where a page lasts less time. Fault-injected SD timeout and
card-removal recovery still need device testing.
