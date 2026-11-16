# MSAMPLER performance capture

Build with `cmake -S sources -B build-opt -DCMAKE_BUILD_TYPE=Release
-DRELEASE_DEBUG_SYMBOLS=ON`, flash the resulting firmware, and retain its matching
ELF. RAM monitoring is enabled on Advance independently of ENABLE_PROFILING and
produces no serial output.

Start OpenOCD with the workspace's probe configuration:

```
openocd -f interface/stlink.cfg -f target/stm32h7x_dual_bank.cfg -c 'adapter speed 1000' -c 'stm32h7x.cpu0 configure -work-area-backup 1'
python3 tools/capture-streaming-perf.py build-opt/Adapters/adv/picoTrackerAdvance.elf --gdb /usr/bin/gdb --seconds 30 --output streaming-perf.json
```

Start the eight-note project before capture and use the buttons during it.
The script briefly halts for two coherent snapshots, explicitly resumes through
OpenOCD, and disconnects each time. It writes JSON snapshots and a summary of
interval counts, means, maxima, and elapsed time shares. It neither flashes nor
resets the device. Use --gdb and --target to override the debugger/server.

The script validates the flash firmware-info header against the supplied ELF,
including its build CRC and length, using memory reads. **Do not run target-side
CRC/compare-sections on live firmware with work-area-backup disabled.** OpenOCD's
default 64 KiB scratch area begins at 0x20000000, overlapping the Audio task stack
and other DTCM buffers. A target-side verification algorithm can overwrite them.
Enable backup for live debugging; capture itself executes no target algorithm.

Counters are cumulative since boot. The script changes captureEpoch while halted;
each metric's writer starts a new windowMaxCycles when it next observes that epoch.
The summary reports only window maxima belonging to the current capture epoch.
maxCycles is a separate lifetime maximum and can include loading/startup.
queueHighWater is a lifetime watermark. Failed validation still explicitly resumes
in the debugger's finally block.

Metrics include:

- audio: audio buffer preparation and MIDI flush elapsed time.
- streamRead: request service, with streamOpen, streamSeek, streamDataRead stages.
- streamQueueWait: enqueue-to-service delay; queueFull and queueHighWater describe
  queue pressure. Idle cached handles close after 500 ms, checked every 250 ms;
  instrument release explicitly drains its requests and closes cached handles.
- sdWait: DMA completion elapsed time; sdReadTransfers, sdReadSectors and
  sdBounceTransfers expose transfer batching, while sdErrors/sdTimeouts count
  driver errors. SD operations are serialized with a mutex; task-context DMA
  waits block on an IRQ semaphore. Boot retains its polling fallback and original
  IRQ priority until the scheduler starts. Unaligned or non-DMA-accessible buffers
  use one shared 4 KiB aligned AXI staging buffer for up to eight sectors per DMA.
- pageMisses: voices retired because their required page was unavailable. A missing
  page fades the last sample to silence over 32 frames; playback never freezes its
  source cursor indefinitely. interpolationMisses counts unavailable adjacent
  frames (the current sample is held for that interpolation).
- uiDispatch: full UI event handling; uiGap includes ordinary idle time.
- inputScanGap: spacing between GPIO scans (100 Hz target).
- inputQueueWait: sampled button event to UI dispatch; inputDispatchLatency adds
  dispatch work. Neither includes the delay before GPIO sampling nor asynchronous
  panel scanout. Events not generated during a capture produce no input timings.

These scopes include blocking and preemption, so elapsed percentages overlap and
must not be added as exclusive CPU utilization. Debugger halts can themselves
cause audio artifacts; exclude snapshot boundaries when judging audible glitches.
audioUnderruns counts DMA callbacks that submitted silence while playing; a clean
output underrun count alone does not prove every streamed voice stayed supplied.

DWT durations wrap after about nine seconds at 480 MHz. Individual longer stalls
cannot be measured accurately. Counts wrap at 2^32. Do not reset cumulative
counters while firmware is running; reboot for a fresh lifetime measurement.
