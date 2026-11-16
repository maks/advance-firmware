# MSAMPLER profiling findings

Captured on the connected Advance with optimized Release firmware and debug
symbols, during the user's eight-note Basic Piano project playback.
See the adjacent raw capture and asset-layout files.

## Measurements

30-second capture; core frequency reported as 479,808,000 Hz.

| Scope | Completed calls | Mean elapsed time | Sum of elapsed times / capture interval |
| --- | ---: | ---: | ---: |
| Audio buffer preparation | 1661 | 9.13 ms | 50.55% |
| Stream request (open/seek/read/close) | 549 | 39.45 ms | 72.20% |
| UI event dispatch | 830 | 3.55 ms | 9.82% |

These are elapsed scope times including preemption and waits, not exclusive
CPU percentages. They overlap and must not be added together. The nominal
capture interval excludes debugger overhead. Audio's lifetime maximum reached
9.93 ms. Stream's lifetime maximum was 225.26 ms, and UI dispatch's lifetime
maximum was 2.02 seconds; those maxima may precede this interval (including
loading) and do not establish the worst playback-only latency.

Output underruns increased from 31 to 34. Snapshot halts can themselves cause
underruns, so three is not a reliable count of spontaneous playback failures.
The current counters do not detect per-voice missing pages, and uiGap includes
normal idle time. Input-to-display latency is not measured directly.

## Identified issues

1. **Storage waits monopolize a task above UI priority.** SampleStream runs at
   priority 3, input at 2, and UI dispatch at 1. SD_DMA_read and card-status
   checking poll in loops while DMA/card operations complete. A queued burst
   keeps the worker runnable and prevents lower-priority input and redraw work
   from running. Audio (priority 6) can continue during this starvation.
2. **Unaligned reads lose multi-sector DMA.** The inspected loaded piano asset
   has dataOffset=78. Stream pages are aligned, but source offsets are not sector
   aligned. FatFs first copies the partial sector, advancing the destination by
   a non-32-byte-aligned amount. SD_DMA_read then takes its scratch-buffer branch
   for subsequent sectors: one DMA operation and busy wait per 512 bytes instead
   of one multi-sector transfer. This path is established by source and the
   observed asset layout; per-path transfer counters have not yet been added.
3. **Every page reopens and seeks its WAV.** This repeats path lookup, FAT traversal,
   and file setup, including when consecutive requests target the same file.
4. **Rendering repeats page bookkeeping per sample.** Each interpolated output
   frame invokes readFrame twice, scanning atomic page states and checking page
   recycling. The measured 9.13 ms average audio preparation time leaves a
   substantial fraction of CPU time unavailable to storage and UI.
5. **A missing page silently inserts silence and freezes the source cursor.**
   readFrame failure does not advance position or increment a counter. The
   output DMA underrun count therefore cannot establish glitch-free streaming.

## Recommended implementation order

1. Replace SD completion polling with a bounded RTOS wait and appropriate
   interrupt completion notification, preserving timeout/error recovery and
   filesystem serialization. Blocking lets UI run while the card/DMA works.
2. Read aligned source sectors through a small DMA-safe aligned staging buffer,
   then copy PCM bytes into pages. Confirm multi-sector transfers with counters.
3. Add request queue delay/depth, page-miss counts and per-stage I/O timing;
   capture playback-only maxima and input queue age. Repeat this same project.
4. Retain a bounded number of file handles for active streams and avoid repeated
   seeks when file position already matches the next request.
5. Hoist page lookup/recycling out of the per-frame interpolation path and
   measure render time again before changing buffer sizes or scheduling policy.

This investigation did not change or reflash firmware. The device was explicitly
resumed after each debugger inspection.
