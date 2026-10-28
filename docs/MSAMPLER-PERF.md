# MSAMPLER performance capture

Build Release with `-DRELEASE_DEBUG_SYMBOLS=ON`, flash it,
and retain the matching ELF. RAM monitoring is enabled on Advance regardless
of ENABLE_PROFILING and produces no serial output.

Start OpenOCD with the board's usual probe configuration, for example:

```
openocd -f interface/stlink.cfg -f target/stm32h7x_dual_bank.cfg
python3 tools/capture-streaming-perf.py build-adv/Adapters/adv/picoTrackerAdvance.elf --gdb /usr/bin/gdb --seconds 30
```

Start the eight-note project before capture and exercise the UI during it.
The script attaches twice, briefly halts for coherent snapshots, explicitly resumes through OpenOCD and disconnects each time, and saves both snapshots to streaming-perf.txt. It neither
flashes nor resets the device. Use --gdb and --target to override tools/server.
Debugger halts can themselves cause audio artifacts; exclude the capture
boundaries when judging audible glitches. Counters are cumulative since boot.

Subtract totalCycles and count between snapshots for interval means. Convert
cycles to milliseconds using cpuHz. maxCycles is the lifetime maximum;
audioUnderruns counts DMA callbacks that had to submit silence while playing.
Audio, streamRead and uiDispatch measure elapsed scope time including blocking
and preemption, not exclusive CPU usage. uiGap measures spacing between event
dispatches, including normal idle time; it is not input queue latency. DWT
32-bit durations wrap after about 9 seconds at 480 MHz, so individual longer
stalls cannot be measured accurately. Counts wrap at 2^32. Do not reset counters
from GDB while firmware is running. A reboot starts a fresh measurement.

This initial instrumentation does not count individual missing MSAMPLER pages
or measure input-to-display latency. A clean audioUnderruns count alone does
not prove that every streamed voice stayed supplied.
