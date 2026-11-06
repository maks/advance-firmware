# USB audio master output in device mode

Issue: [#475 — USB audio out in Device mode](https://github.com/xiphonics/advance/issues/475)

Branch: `475-uac-master-output`, in the existing checkout.

## Agreed scope

- Advance USB device mode only; USB audio host support is outside this feature.
- An explicit, persisted **Analog / USB** output setting.
- Selecting USB replaces physical audio output rather than playing both.
- USB audio coexists with existing USB MIDI and CDC features.
- USB carries the final stereo master mix, including song/mixer volume and effects.
- Codec output volume does not change the USB recording level.
- Start with UAC2, stereo interleaved 16-bit PCM at the existing 44.1 kHz rate,
  using the bundled TinyUSB stack.

Proposed defaults: Analog is selected initially. USB remains selected when
disconnected or unavailable, physical output stays muted, and the UI indicates
that USB output is unavailable. USB host MIDI behavior remains available.

## Existing implementation

- `sources/Services/Audio/AudioOutDriver.cpp` produces clipped, interleaved stereo
  PCM after rendering the master mix.
- `sources/Services/Audio/AudioDriver.cpp` queues PCM and applies existing
  `silenceOutput` behavior, including silence during stem rendering.
- `sources/Adapters/adv/audio/advAudioDriver.cpp` uses SAI DMA completion to pace
  the audio task and therefore playback/sequencing.
- `sources/Adapters/adv/audio/tlv320aic3204.c` controls codec gain, muting,
  headphone detection, physical output selection, and speaker amplifier state.
- `sources/Adapters/adv/usb/usb_descriptors.c` currently describes CDC and MIDI.
- `sources/Adapters/adv/config/tusb_config.h` enables device CDC and MIDI.
- `sources/Adapters/adv/gui/advEventManager.cpp` owns USB task servicing and
  device/host role transitions.
- `sources/Externals/tinyusb/CMakeLists.txt` already includes the audio device
  driver. The bundled stack identifies itself as TinyUSB 0.18.0 and provides
  UAC2 descriptor helpers and audio IN flow control.

## Implementation steps

### 1. Validate USB enumeration and sustained streaming

Enable one audio device function in `tusb_config.h`. Extend the composite USB
descriptors with an AudioControl interface, stereo AudioStreaming interface,
fixed clock source, and asynchronous isochronous IN endpoint, named
**picoTracker Master**. Include alternate setting 0 with no streaming endpoint
and alternate setting 1 for streaming.

Preserve the existing MIDI and CDC interfaces. Check endpoint availability,
controller FIFO allocation, descriptor lengths, interface associations, and
full-speed packet capacity. Keep any retained high-speed descriptors consistent
with their interval and packet-size requirements. Review device revision and
descriptor caching during host validation without arbitrarily changing the
assigned VID/PID.

Implement the clock queries advertised by the descriptors and advertise only
controls actually supported. Do not copy the mono microphone descriptor
unchanged: use stereo channels and an appropriate terminal topology for the
master mix. First validate enumeration and streaming with a known signal.

### 2. Capture the final master PCM

Add an Advance-specific handoff after mixing, effects, clipping, and existing
output-silence handling. Copy the PCM into a bounded, statically allocated queue;
audio preparation must never wait for USB or retain pointers to reusable mixer
buffers.

Preserve left/right ordering and 16-bit PCM samples. Preserve existing silence
semantics, including muted stem-render buffers. Inspect render timing so offline
rendering cannot flood the USB queue with audio produced faster than real time.
Apply no extra gain based on codec output volume.

### 3. Bridge the audio and USB clocks

Drain queued PCM into TinyUSB's audio FIFO from the USB task or its audio
callbacks. Keep TinyUSB audio access owned by the USB task; the audio task only
publishes PCM through the handoff.

Use audio IN flow control to accommodate the independent codec and USB clocks.
At full speed, nominal packets contain 44 or 45 stereo frames per millisecond
(176 or 180 bytes). Verify the bundled driver's packet sizing and descriptor
maximum rather than assuming fixed-size packets.

Size the handoff and TinyUSB FIFO for the actual variable mixer chunk sizes,
task scheduling delays, and flow-control requirements. Establish a startup fill
target and a bounded overflow recovery policy. Count underruns, overruns, and
discarded frames; emit diagnostics outside time-critical paths.

### 4. Add output routing and persistence

Add the Analog/USB configuration variable and expose it in the appropriate
output settings UI. Default to Analog and persist the selection.

Keep SAI/DMA and the codec clock running while USB is selected: stopping the
existing driver would stop the callbacks that pace playback. Mute physical
outputs and disable the speaker amplifier using a routing policy respected by
playback startup and headphone detection.

Keep USB samples separate from silence applied solely to suppress analog output.
The existing stem-render silence must still affect USB output. Avoid codec I/O
or blocking output transitions in DMA interrupts and audio preparation.

### 5. Handle streaming and USB lifecycle

Distinguish USB output selection, device enumeration, and an active streaming
alternate setting. Send silence while a recording stream is active but playback
has no audio. Discard audio when there is no active USB consumer rather than
accumulating stale playback.

Handle streaming start/stop, Analog/USB selection changes, disconnect, suspend,
resume, and device/host role transitions. Reset queues and stream state at the
appropriate boundaries so reconnection cannot replay stale samples. Follow the
selected output policy throughout transitions and restore analog output when
Analog is selected.

### 6. Validate and prepare manual testing

Add focused tests for queue integrity, frame alignment, routing state, silence,
and transition resets. Extend relevant existing audio-silence coverage where
appropriate. Run clang-format on changed C/C++ files, relevant host tests, and
an Advance firmware build.

Hardware acceptance checks:

- Enumerates as a stereo recording input alongside MIDI and CDC.
- Records at 44.1 kHz/16-bit with correct stereo ordering, effects, and mixer
  volume; changing codec output volume does not alter USB samples.
- USB selection mutes physical outputs, including the speaker, across playback
  restarts and headphone insertion/removal.
- Analog selection restores normal physical output behavior.
- Playback stop/start and streaming stop/start produce clean transitions and
  silence without replaying old samples.
- Disconnect/reconnect, suspend/resume, and device/host switching reset state
  correctly and preserve existing host MIDI functionality.
- Simultaneous audio, MIDI, and CDC traffic does not cause audio starvation or
  unacceptable MIDI timing changes.
- An extended recording reveals no progressive queue drift, dropped samples,
  clicks, or growing latency under representative playback load.

## Main risks and implementation order

The main risks are buffering variable audio chunks across independent clocks,
USB scheduling under concurrent traffic, and maintaining physical-output muting
across existing codec state changes.

Validate enumeration and sustained USB streaming first, then connect the real
master PCM, integrate routing and persistence, and complete lifecycle and
hardware acceptance checks. Keep the existing audio driver as the playback
timing source throughout this feature.

## References

- [Apple: USB audio device design considerations](https://developer.apple.com/documentation/technotes/tn3190-usb-audio-device-design-considerations)
- The bundled TinyUSB audio driver and descriptor helpers are the implementation
  reference; current upstream examples may require APIs absent from this version.
