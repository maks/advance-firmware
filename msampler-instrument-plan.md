# Advance streamed multisample instrument implementation plan

Status: proposed implementation plan; firmware implementation is not part of this change.
Prepared against the current checkout on 2026-10-28. Working name/type: `MSAMPLER` / `IT_MSAMPLER`.

## 1. Goal and agreed first-version scope

Add an Advance instrument that selects a WAV from a DecentSampler `.dspreset`
using note and velocity, then plays it directly from SD without loading the
complete sample into RAM. Make triggering independent of SD seek latency by
preloading attacks. Give each loaded instrument instance at most **4 MiB
(4,194,304 bytes)** from the existing sample-storage SDRAM, shared by all
channels playing that instance. This is an upper bound, not a promise that every
instrument slot can allocate 4 MiB simultaneously.

The user confirmed:

- Key ranges and velocity layers only. Defer overlapping sample layering,
  round robin, and all loops.
- One voice per tracker channel, with the same instrument usable on multiple
  channels. Advance currently has eight song channels.
- Unpacked `.dspreset` plus WAV files only; no `.dslibrary` packages.
- WAV input: signed 16-bit PCM, mono or stereo, 44,100 Hz only.
- Note-off stops playback using a short click-prevention ramp.
- No DecentSampler ADSR, filters, effects, modulators, bindings, or UI support.

Playback is forward, pitched, non-looping, and ends at its selected end frame or
on release/cut. An implementation ramp is not an imported envelope. Existing
tracker channel/mixer controls remain available where already applicable; do
not interpret DecentSampler DSP parameters as tracker controls.

## 2. Evidence and references

### Repository findings

| Area | Existing implementation | Consequence |
| --- | --- | --- |
| Instrument interface | `sources/Application/Instruments/I_Instrument.h` | `Start` carries channel/note/start mode but no velocity. `Release`, `Stop`, `Render`, pitch multiplier, and buffer-layout hooks exist. |
| Modern sample source | `SampleSourceInstrument.{h,cpp}`, `Application/Player/AudioVoice.cpp` | Useful pitch/render/lifecycle patterns, but its source assumes resident sample memory. Do not pass a streaming page off as a complete `SoundSource`. |
| Instrument allocation | `InstrumentBank.{h,cpp}` | Existing fixed-capacity allocation and persistence need the new type, including correct maximum object size/alignment. |
| Sample SDRAM | `Adapters/adv/system/advSamplePool.{h,cpp}`, `SamplePoolStorage.h` | 32 MiB `sampleStore1` in `.SDRAM2`, 16 MiB `sampleStore2` in `.SDRAM1`; allocation is currently append-based and deletion compacts data. Streaming reservations must participate in this ownership scheme. |
| WAV handling | `WavHeader.{h,cpp}`, `WavFile.{h,cpp}` | Reuse RIFF chunk parsing where suitable; existing decoding supports more formats than this feature. `WavFile` uses shared conversion buffers and file reads, so it is not an audio-safe streaming abstraction. |
| Preview playback | `Application/Audio/AudioFileStreamer.{h,cpp}` | Existing streaming preview is a reference, not the real-time instrument architecture; its render path can read files. |
| Filesystem | `System/FileSystem/{I_File,FileHandle,FileSystem}.h`, `Adapters/adv/filesystem/advFileSystem.{h,cpp}` | Owned file handles exist, but seek/read error handling and concurrent access require review. |
| FatFs settings | `Adapters/adv/config/ffconf.h` | `FF_FS_REENTRANT=0`, `FF_FS_LOCK=10`, `FF_USE_FASTSEEK=1`, `FF_FS_TINY=0`; the adapter file pool also has ten slots. |
| SD implementation | `Adapters/adv/sdcard/sd_diskio.{c,h}`, `sd_diskio_config.h` | Already uses SDMMC1 DMA, but waits in polling loops; aligned transfers differ from the one-sector scratch fallback. Current timeout is 1,000 ms. |
| Scheduling | `Adapters/adv/audio/advAudioDriver.cpp`, `Adapters/adv/gui/advEventManager.cpp` | Audio rendering runs at FreeRTOS priority 6; UI/input/record tasks also access shared services. A streaming worker must yield and stay below audio priority. |
| Memory/MPU | `Adapters/adv/STM32H743XX_FLASH.ld`, `Adapters/adv/Core/Src/main.c` | Sample SDRAM is cacheable; AXI SRAM is configured non-cacheable. D3 is a separate 64 KiB bank. Named sections are `NOLOAD` and need explicit initialization. |
| Allocation policy | `sources/config/no_heap.h` | Use bounded/static storage and ETL containers; no general-purpose dynamic allocation for the new runtime. |

TokenSave status/context queries reported this checkout is not initialized;
these findings therefore come from direct source inspection, not an indexed
graph. No index was created or updated.

### Format and behavior references

1. [Full local specification](refs/decentsampler-format-spec.md): section 1,
   section 3 (`groups`, `group`, `sample`, volume/pan and looping), and the
   preset-validation section. Primary local reference for detailed attributes.
2. [Additional local guide](refs/Decent_Sampler_Developer_Guide.md): a concise
   guide marked format version 1.17. It contains escaped XML examples and a
   `loobEnabled` typo; these are not syntax to reproduce in the parser.
3. [Official format overview](https://decentsampler-developers-guide.readthedocs.io/en/stable/introduction.html):
   online fallback for gaps/conflicts; the consulted stable overview identifies
   itself as 1.40.1. Follow its links to the relevant attribute reference when
   verifying details; do not rely on undocumented defaults.
4. Local binary reference:
   `refs/Decent_Sampler-1.40.1-Linux-Static-x86_64/DecentSampler` and
   `DecentSampler.so`. The README identifies standalone and VST2 builds; ELF
   inspection identifies a stripped x86-64 executable. Read-only string
   inspection finds mapping attribute names, `diskStreamingPreloadBufferSize`,
   and preset-validation UI text. These establish useful test targets, not
   its buffering algorithm or undocumented playback semantics. No binary was
   executed or reference audio rendered during planning.
5. [ST RM0433 reference manual](https://www.st.com/resource/en/reference_manual/dm00314099-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
   and [ST's STM32H743 SD example](https://github.com/STMicroelectronics/STM32CubeH7/blob/master/Projects/STM32H743I-EVAL/Examples/SD/SD_ReadWrite_IT/readme.txt):
   verify the exact bus-master reachability and cache rules for any DMA target.
   ST's example recommends non-TCM SRAM such as AXI SRAM for SD buffers. The
   large manual could not be fully retrieved by the browser during planning;
   DMA reachability beyond the conservative AXI path remains a validation gate.

Do not assume that DecentSampler's desktop disk engine can be reproduced from
binary strings. Use the binary for behavioral comparisons with small fixtures.

## 3. Import contract and compatibility policy

### Supported subset

Parse actual XML rooted at `<DecentSampler>`, with `<groups>`, `<group>`, and
`<sample>` mappings. Ignore the entire `<ui>` subtree without loading its assets.
Flatten supported inherited attributes at import time; the audio thread sees
only immutable resolved zones and a bounded note lookup index.

| Attribute | Planned behavior |
| --- | --- |
| `path` | Required; resolve relative to the `.dspreset` directory, normalize separators and bounded path storage, and deduplicate identical resolved files. |
| `rootNote` | Required; translate MIDI note number to tracker pitch without an octave-label error. Version one accepts numeric MIDI notes 0–127; symbolic note names are unsupported unless explicitly added and tested. |
| `loNote`, `hiNote` | Inclusive MIDI range; documented defaults 0 and 127. Validate ordered bounds. |
| `loVel`, `hiVel` | Inclusive 0–127 range; documented defaults 0 and 127. Runtime note-on velocity is 1–127; MIDI velocity zero is note-off. |
| `start`, `end` | Static source-frame bounds, defaulting to 0 and final frame. Normalize inclusive external end to an exclusive internal bound with checked arithmetic. Preload from the effective start, not necessarily file frame zero. |
| `enabled` | Honor disabled groups; do not make hidden alternatives audible by treating disabled groups as enabled. |
| `playbackMode` | Always use the bounded streaming engine, including for `memory` requests; report the override. No full-file fallback. |

Initial conservative proposal: non-default tuning, key tracking, gain/pan, and
velocity-to-amplitude settings are outside the playback subset. Detect and
summarize them during import. If simple static tuning/gain are later approved,
add explicit tests: the full spec says volume at the three hierarchy levels is
multiplicative, while ordinary pan inherits. Do not accidentally implement them
by copying all parent attributes over the child.

This is a **documented subset importer**, not general DecentSampler compatibility:

- Disjoint velocity zones are supported. If two effective zones can match the
  same note and velocity, fail import with zone locations and ranges rather
  than choosing one silently. Adjacent inclusive ranges sharing a boundary are
  overlaps too. Deduplicated file assets may still have multiple disjoint zones.
- Gaps are legal and play silence; note changes do not select a nearest zone.
- Explicit looping, round-robin/random selection, release/legato/continuous
  triggers, keyswitch/CC conditions, delayed triggers, non-normal sample modes,
  and oscillator sound sources must not silently become ordinary attack zones.
  Reject mappings requiring these behaviors, with a useful diagnostic.
- Embedded WAV loop markers do not enable looping in this version. Advertise
  one-shot-only playback and report ignored loop metadata. A preset explicitly
  requiring loops is rejected; `loopEnabled=false` is accepted.
- Ignore ADSR, filter, effect, modulation, binding, and UI configuration, with
  one consolidated compatibility notice for sound-affecting omissions. UI
  images/control layouts never become Advance UI. Bindings are never executed.
- Reject unsupported audio encodings/rates/channels before activation. No
  automatic resampling or conversion of SD assets in version one.
- Reject malformed/truncated XML, missing assets, invalid numbers/NaN, path or
  metadata overflow, impossible frame bounds, and unrepresentable file offsets.
  Normalize paths without depending on process/global current-directory changes.

Use bounded XML token/SAX parsing, or prove the existing tinyxml2 configuration
can operate within a bounded import arena. Do not build an unbounded DOM in
internal RAM. Bound input bytes, nesting, attribute lengths, zone count, unique
assets, and path bytes, and report which limit was exceeded. A useful initial
metadata ceiling is 512 zones / 512 files, subject to actual descriptor sizes
and the attack-cache capacity check; this does not guarantee that 512 attacks
will fit. Never truncate a preset and call it successfully loaded.

## 4. Components and integration

Proposed new responsibilities, with names adjustable to repository conventions:

- `MSamplerInstrument`: instrument variables, lifecycle, per-channel voice
  activation, pitch control, render, release, persistence, and import status.
- `DecentPresetReader`: bounded parsing and inheritance resolution, subset
  diagnostics, overlap detection, asset deduplication, and note/velocity lookup.
- `StreamedWavSource`: validated immutable file metadata: format, data offset,
  frame count, source-frame bounds, and source identity. Reuse `WavHeader`
  validation without relying on mutable shared `WavFile` scratch buffers.
- `SampleStorageReservation`: stable reservations inside the current sample
  SDRAM stores; accounting and safe release shared with resident sample loading.
- `SampleStreamService`: bounded request queues, file-handle ownership,
  deadline-based read scheduling, buffer publication, cancellation, and metrics.
- Advance SD completion support: event-driven DMA completion/error/timeout and
  centralized cache maintenance, shared with existing filesystem users.

The service is global, not one task or DMA buffer set per instrument. Instrument
descriptors and attacks are per instrument; active voice cursors are per channel.
There are at most eight active instrument voices globally, not eight voices
allocated for every instrument slot.

Append `IT_MSAMPLER` before `IT_LAST` without renumbering existing types. Update
type names, bank construction/destruction/clone, fixed object-pool sizing,
instrument selection/editor/import, persistence, build lists, and every switch
or type predicate that depends on instrument kind. Keep implementation
Advance-only; provide intentional unsupported behavior or build guards for
WASM/host configurations rather than including HAL code in common code.

## 5. Memory architecture and allocation safety

### Observed internal-memory baseline

These are existing build artifacts, **not fresh measurements of this HEAD**.
Rebuild both Debug and Release before committing to addresses or capacities.

| Bank | Capacity | Existing artifact observations | Allocation decision |
| --- | --- | --- | --- |
| AXI `RAM` at `0x24000000` | 512 KiB | Debug `.DATA_RAM` ends at `0x2407b3b0` (~19.1 KiB remaining); Release ends at `0x240793c8` (~27.1 KiB remaining). | Only a small shared DMA staging area; no bulk cache or worker stack. |
| DTCM at `0x20000000` | 128 KiB | Debug heap/stack reservation ends at `0x2001ed80` (~4.6 KiB remaining). | No new bulk buffers or worker stack; preserve main-stack headroom. |
| D2 at `0x30000000` | 288 KiB | `.RAM_D2` uses `0x2d000` (180 KiB), currently owned by Dattorro reverb; 108 KiB remains. | Candidate for small CPU playback windows, explicitly budgeted without touching reverb storage. |
| D3 at `0x38000000` | 64 KiB | `.RAM_D3` has no payload in either inspected map. | Worker stack, TCB/queues, small CPU metadata; never assume this is an SDMMC1 DMA target. |
| ITCM | 64 KiB | Existing `.fastcode` differs by build. | No new placement until profiling proves need. |

Artifact paths: `build-adv/Adapters/adv/picoTrackerAdvance.map` and
`build-opt/picoTrackerAdvance.map`.

Initial **global**, not per-instrument, budget proposal:

- AXI SRAM: two 4 KiB, 32-byte-aligned SD staging buffers (8 KiB total), with
  total new AXI usage capped at 10 KiB including alignment and small globals.
  Require at least 8 KiB remaining AXI headroom in fresh Debug and Release maps.
- D2: at most 32 KiB for optional hot playback windows, e.g. two 2 KiB windows
  per active channel. These are CPU-filled/read, not assumed DMA targets.
- D3: initial cap 16 KiB for a static worker stack (start with 8 KiB), queues,
  synchronization objects, and channel control. Measure stack high-water usage.
- No new DTCM allocation by default. Put large zone/path tables, stream cache,
  and import working storage in the instrument SDRAM reservation; shared import
  scratch, if needed, gets a separately bounded external allocation and report.

If fresh maps fail these budgets, reduce to one 4 KiB DMA buffer or move a
measured, suitable CPU-only allocation from AXI to D3 before adding the feature.
Do not take display DMA storage or expand into unspecified memory. Introduce
section start/end symbols and linker assertions for budget limits; explicitly
initialize all new `NOLOAD` state. Include file-object/pool growth in the map
budget, since adding FatFs slots can consume substantial SRAM.

### Shared sample-store ownership

The existing two sample stores occupy their entire linker sections already.
Do not add another 4 MiB `.SDRAM*` array beside them or simply keep a pointer to
their currently unused tail: resident sample import could overwrite it.

Recommended minimal allocator change: resident samples grow from each store's
low end; streaming reservations occupy stable extents from the high end.
`storeLimit1_`/`storeLimit2_` become the boundary below streaming extents.
Track reservations separately, including alignment, free holes, owner, and
generation. Reuse holes on release; only raise the resident limit when the
lowest reservation boundary is genuinely free. Alternatively use a common
extent allocator if tail reservations become too fragmented, but do not
rewrite resident sample allocation without evidence it is needed.

Resident sample compaction must remain entirely below these boundaries and
must not move active stream arenas. Update capacity checks, free-space display,
reset, deletion, clone, project switch, import failure rollback, and exhaustion
diagnostics together. A released arena is reusable only after the audio thread
and worker acknowledge its retirement. No memmove of a published stream buffer.

Use at most 4 MiB per loaded instance, including its metadata, attacks, optional
tails, and stream pages. Cloned slots use independent reservations initially;
all eight channels sharing a slot share its immutable preload data. Release
arenas when instruments are removed or unloaded; do not reserve 4 MiB for every
empty bank slot. Loading must fail cleanly when combined resident samples and
stream reservations exceed the 48 MiB sample-store capacity.

### Example 4 MiB instrument arena

| Purpose | Example ceiling |
| --- | --- |
| Zone/file descriptors, interned paths, lookup and bookkeeping | 256 KiB |
| Pinned attack cache, optional boundary data | 2,816 KiB |
| Rolling decoded stream pages | 1,024 KiB |
| Total | 4,096 KiB |

This is a tunable partition with strict total accounting. For example, 1 MiB
provides sixteen 64 KiB decoded pages, enough for two pages per channel if all
eight channels play this instance. Smaller instruments can reserve less, but
must retain the stream capacity needed for their admitted channel count.

Pin an attack for **every selectable effective start**, not only recently played
notes. Deduplicate identical file/start spans. Load a short complete sample if
it fits the same cache budget. Tail caching is optional and lower priority:
with loops/release samples excluded, reaching EOF is sequential and can be
prefetched normally. Arbitrarily caching the end of every WAV wastes attack
capacity and does not solve seek stalls. Reserve an interpolation guard frame
and exact EOF behavior instead. Future loops would require separate loop-entry
and loop-exit caching and are outside this plan.

## 6. Timing model, cache sizing, and supported pitch range

For source rate `Fs`, channels `C`, source bytes per sample `B=2`, and pitch
ratio `r`, consumption is `Fs * C * B * r` bytes/s. At unity pitch, 44.1 kHz
stereo consumes 176,400 bytes/s; eight voices consume 1,411,200 bytes/s before
filesystem overhead. At +24 semitones, eight stereo voices consume
5,644,800 bytes/s. Random seeks, fragmented FAT chains, and recording compete
with this traffic; a sequential card benchmark alone is insufficient.

Initial proposed supported pitch window is ±24 semitones relative to a zone's
root, including tracker pitch commands. Validate the mapping against this
window on import and define out-of-range live pitch handling explicitly (clamp
with diagnostic counter, not an unbounded streaming rate). Raise the window
only after latency/throughput measurements support it.

For each zone, size its pinned attack to cover the measured worst accepted
startup service delay, including all simultaneous triggers, open/seek/FAT
lookup, SD transaction, publication, and a safety margin:

`attackBytes >= ceil(Fs * C * B * maxRatio * startupCoverageSeconds)`.

Use 50 ms as an initial design target to measure, not a claim about all cards.
At unity stereo this needs 8,820 bytes, or 35,280 bytes at 4x pitch, before
alignment/guard frames. A 2,816 KiB attack partition cannot cover hundreds of
stereo zones at that maximum rate and target. Calculate capacity on import and
reject an instrument that cannot meet its declared supported envelope; do not
quietly leave some zones without a pinned start or shrink coverage to zero.
Report required/available memory and the limiting zone count. Supported cards,
cache budgets, pitch limits, and preset limits must be published together.

Rolling-cache low-watermarks cover the worst **aggregate** refill delay across
all voices. Schedule by time-to-exhaustion rather than fixed round robin.
Each source page records frame range, generation, valid count, and guards.
As an example, 64 KiB decoded stereo covers ~372 ms at unity and ~93 ms at 4x.
Two pages provide useful reserve only if the complete scheduling cycle can
refill them in time. Measure the cost of assembling a page from sixteen 4 KiB
reads; tune DMA transaction size within the verified SRAM budget.

No finite cache can guarantee uninterrupted playback through arbitrary SD
stalls. Define the accepted card/service-latency envelope and deterministic
underflow handling rather than promising that 4 MiB eliminates every dropout.

## 7. Read path, DMA, and filesystem arbitration

Preferred first implementation:

`SD file -> SDMMC1 IDMA -> shared AXI staging -> CPU copy -> SDRAM page -> renderer`

Optional measured optimization:

`published SDRAM page -> CPU copy to small D2 hot window -> renderer`.

Use CPU copies first; these are small, bounded, and avoid another DMA engine's
ownership/cache complexity. Add MDMA only if profiling justifies it and after
proving both source/destination reachability and cache ordering. D3 and TCM are
not the baseline SD DMA destinations. Direct DMA into cacheable SDRAM is a
future optimization, not a prerequisite.

Required filesystem work:

1. Audit all SD callers, including preview, recording/resampling, project saves,
   sample import, directory browsing, USB mass storage, and raw disk paths.
   `FF_FS_REENTRANT=0` cannot safely coexist with uncoordinated worker calls.
2. Establish a single filesystem/storage arbitration policy. A global
   priority-inheriting task mutex around complete FatFs operations is the
   smallest candidate; split long application reads/writes into bounded chunks
   so refill deadlines are serviceable. Protect directory/open/close/seek and
   the shared file pool too. Never hold this mutex in an audio callback.
3. Prefer routing long background operations through the storage scheduler
   where a mutex alone cannot bound lock holding. Decide and document whether
   concurrent recording is supported from measured read/write latency. If it
   is not, block that combination in the UI with a clear reason.
4. Suspend/quiesce streams before handing the card to USB mass storage or
   unmounting it. Do not allow raw block writes beside an active mounted volume.
5. Replace streaming-path DMA busy waits with completion/error notifications
   and bounded task waits. Keep ISR work to status/notification; no filesystem
   calls, parsing, or logging. Handle transfer abort and late interrupts before
   reusing buffers. Existing non-streaming SD behavior must keep working.
6. Preserve synchronous FatFs semantics in the worker: hardware DMA does not
   make `f_read` an asynchronous caller API. Only the audio/worker boundary is
   asynchronous. Check actual bytes read, seek result, timeout, and card state.

Use aligned, whole-sector transfers where possible. A WAV's `data` offset is
often not sector-aligned: aligning the RAM address alone is insufficient.
Maintain a sector-aligned file-byte reader with leading/trailing partial-sector
handling, then extract complete PCM frames into decoded pages. Never assume a
44-byte WAV header or interpret chunk bytes past `data` as PCM. Do not bypass
FatFs using raw sector arithmetic unless file fragmentation is accounted for.

Keep DMA buffers on dedicated whole cache lines, with clear exclusive ownership.
Current AXI MPU settings make the proposed staging non-cacheable; assert those
settings rather than depending on incidental attributes. For any cacheable
DMA target, implement the appropriate pre-transfer clean/invalidate and
post-transfer invalidate sequence centrally, with barriers and no unrelated
objects sharing the affected lines. Audit the existing post-read invalidation
and scratch path before reuse. CPU writes to cacheable SDRAM need publication
ordering, not a DMA invalidate that could discard new data.

### File-handle budget

Keep one handle per actively streamed unique file, with explicit positioning
per request when several channels share it. Do not keep every preset WAV open.
Eight different active voices already consume eight of the current ten slots;
preview/record/import and project operations can exhaust the remainder.

Measure `FIL`/`PI_File` memory before either increasing `FF_FS_LOCK` and its pool
or adding a bounded handle cache. Reserve capacity by operation and fail or
defer low-priority opens cleanly. Avoid closing/reopening every 4 KiB refill;
open and seek latency is part of attack coverage. Use existing fast-seek support
where useful, with bounded cluster-link maps in SDRAM and a safe fallback when
a fragmented file exceeds the map budget. Open files only in worker/import
context, never at note-on in audio context.

## 8. Real-time ownership and rendering

Use a bounded single-producer/single-consumer request path where possible.
Document any additional producer explicitly; UI/import commands must not turn
an SPSC queue into an unsafe multi-producer queue. Statically allocate queues
and task synchronization. Full queues produce a defined retry/error state.

Page states: `Free -> Filling -> Ready -> InUse -> Free`. The worker owns
`Filling`; the renderer reads only immutable `Ready/InUse` data. Publish page
descriptors with release/acquire ordering. Pin attacks for the entire published
instrument lifetime. Do not let the worker overwrite data while a render chunk
or hot-window copy reads it.

Every command/completion carries instrument generation, voice generation,
asset identity, and source-frame range. A rapid retrigger, instrument swap,
clone/delete, project reset, or SD removal invalidates old requests. Late DMA
completion may retire its physical buffer but must never activate an old voice
or write into a released arena. Teardown waits for worker cancellation outside
audio context and then receives an audio retirement acknowledgement.

`Start`, `Release`, `Stop`, `SetPitchMultiplier`, and `Render` perform no SD I/O,
allocation, mutex wait, or blocking trace output. Note-on selects an indexed
zone and starts from its pinned attack in the next normal audio render slice.
The worker immediately begins prefetching beyond that attack. Tied-note and
retrigger behavior must explicitly follow current tracker semantics: ties keep
the selected source/cursor while changing pitch; retrigger restarts that source
at its effective start. A new note performs fresh mapping selection.

Use a wide source cursor (e.g. integer frame plus fixed-point fraction), with
checked source-byte arithmetic. The base increment is
`(sourceRate / outputRate) * 2^((note-rootNote)/12)`, then existing tracker pitch
multipliers apply within the admitted rate bound. Linear interpolation is a
reasonable first implementation; ensure its second frame remains valid across
attack/page/EOF boundaries. Mono/stereo output must match `AudioBufferLayout`.

Release applies a fixed short ramp, initially propose 64–128 output frames
(~1.5–2.9 ms at 44.1 kHz). New note/retrigger/cut tails must fit the one-voice
per-channel contract: a short stored-output ramp may smooth the boundary, but
do not retain a second streaming voice. Define hard stop for project teardown
separately from musical note-off. No DecentSampler envelope data is evaluated.

On underflow, preserve the audio deadline: ramp the affected channel to silence
and retire that voice, increment counters, and post a deferred status message.
Do not repeat an arbitrary old buffer, block, or resume stale audio later.
Unrelated channels continue. On card removal, invalidate all stream generations,
stop safely, and require explicit reload after reinsertion.

## 9. Velocity propagation and tracker behavior

This is required integration, not just parser work. `I_Instrument::Start` and
`AudioVoice::StartInstrument` currently have no velocity argument;
`Player::PlayDirectNote` receives velocity but does not forward it to the audio
instrument. Follow all paths through mixer/player before implementation.

Add a backward-compatible start context or defaulted velocity parameter through
Player -> Mixer -> AudioVoice -> instrument. Existing instruments keep current
behavior. Pass real MIDI/audition velocity for zone selection. Use velocity 127
for tracker notes that do not carry a velocity and document that version one
does not introduce a new phrase velocity column. If an existing command is to
select a velocity layer, specify its execution order separately; ordinary
channel-volume changes must not unexpectedly reselect the WAV midway through
a note. Preserve the selected velocity/source for ties and retriggers.

If the shared modern AudioVoice path automatically introduces envelopes or
filters, choose an explicit source capability/configuration that bypasses those
stages for this initial instrument, or justify reuse of existing tracker-only
controls without importing DS behavior. Do not register the new type as a
modern source blindly and accidentally give it a release envelope instead of
the agreed fixed ramp. Test note-off dispatch through the full player path.

## 10. Advance UI, project persistence, and portability

Use the existing Advance instrument editor and file browser: select MSAMPLER,
choose `.dspreset`, show loading/progress/cancel, selected preset name, usable
zone count, SDRAM usage, and a concise compatibility/error status. Keep audio
activation transactional: failed imports leave the previous instrument usable
where memory permits. If old and new reservations cannot coexist, report that
constraint and perform an explicit unload/load flow rather than corrupting the
old instance. No DecentSampler UI assets or editor are implemented.

Persist the instrument type, bounded preset reference, name, and supported
Advance settings. On project load, resolve and preload outside audio rendering
before marking it ready. Missing or changed files yield a useful unavailable
state; do not silently remap another library. Define a lightweight asset
identity check (size plus metadata/fingerprint) for cached descriptors.

First-version default proposal: reference an unpacked library on SD without
copying the whole library into project sample storage. Resolve relative to a
stable SD library root or project location, never current working directory.
Document that moving/removing that folder breaks the reference. Audit project
export/backup so it either includes the preset and required WAV assets or
explicitly reports external dependencies. Do not accidentally serialize cached
SDRAM data as ordinary resident samples. Clone/delete/unload must release only
their own reservations and handles.

## 11. Implementation sequence and review gates

### Phase 1 — Establish budgets and behavior fixtures

- Rebuild current Advance Debug/Release and retain maps, section sizes, task
  stack high-water readings, audio render timing, and SD latency measurements.
- Make minimal `.dspreset`/WAV fixtures for numeric root notes, inclusive key and
  velocity boundaries, inherited ranges, disabled groups, start/end frames,
  gaps, stereo layout, and rejected features.
- Compare applicable fixture behavior using the local standalone/VST binary.
  Use an isolated test configuration and offline audio/MIDI fixture host where
  available; GUI validation/manual rendering is an acceptable fallback. Do not
  invent a headless CLI or make proprietary binaries a CI dependency.
- Resolve any documentation/binary discrepancy in a recorded compatibility
  decision. Inspect exact RIFF inclusive end behavior and note numbering.

Exit gate: refreshed memory report, reproducible fixtures, explicit accepted
pitch/service envelope, and confirmed scope. No firmware feature starts before
the implementation plan is approved.

### Phase 2 — Sample-store reservations and bounded importer

- Add stable SDRAM reservations, complete accounting, rollback and reset.
- Add parser/resolved zones, WAV validation, overlap checking, indexed selection,
  and deterministic budget calculation. Preload attacks with progress/cancel.
- Bound metadata and parsing scratch; avoid increasing static internal state per
  possible instrument slot. Keep unloaded instances small.

Exit gate: resident sample import/delete still works with mixed reservations;
oversized/invalid presets fail without leaking memory or changing a live slot.

### Phase 3 — Storage service and driver completion

- Implement filesystem arbitration and bounded file ownership first.
- Add static worker below audio priority, event-driven DMA completion, aligned
  staging, source-byte/frame extraction, page states, and generation handling.
- Measure read/seek/open tail latency across qualified cards and fragmentation;
  tune pages, preloads, and scheduling from these results.

Exit gate: concurrent SD operations cannot corrupt FatFs/driver state; stalled
reads do not stall audio. Map and stack checks remain within budget.

### Phase 4 — Instrument rendering and velocity plumbing

- Add bank/type/build integration and minimal renderer with pinned starts.
- Forward velocity, implement note/range selection, pitch, stereo, tie/retrigger,
  release ramps, cancellation, and underflow isolation.
- Keep common APIs backward-compatible and validate existing instruments.

Exit gate: eight concurrent voices, including eight channels using the same
instrument, work within the qualified envelope and 4 MiB instance ceiling.

### Phase 5 — UI, persistence, and operational lifecycle

- Add import/editor states, diagnostics, project save/load and clone/delete.
- Complete SD removal, project switching, preview/record/export arbitration,
  USB mass-storage exclusion, and missing-library recovery.
- Document supported preset subset, external assets, card/pitch limits, and
  limitations. Add instrument usage to the Advance manual.

Exit gate: full manual test checklist passes and resource usage is published.

## 12. Verification and acceptance criteria

Host tests should exercise meaningful behavior using fake files/storage and
controlled delays, without requiring STM32 hardware or the DecentSampler binary:

- XML inheritance/defaults, exact note/velocity boundary selection, gaps and
  overlaps, numeric validation, disabled groups, unsupported trigger/loop/format
  diagnostics, bounded paths, parser depth/size limits, and transactional failure.
- RIFF chunk padding/extra chunks, nonstandard data offsets, mono/stereo PCM,
  partial final sectors, truncated data, frame bounds and arithmetic overflow.
- Page-boundary interpolation and pitch, attack-to-stream continuity, exact EOF,
  deterministic note-off ramps, ties/retriggers, and velocity propagation.
- Reservation coexistence with resident import/compaction, hole reuse, clone,
  unload/reset, capacity reporting, cancellation and failure rollback.
- Fake delayed/reordered completions, queue saturation, SD removal, underflow,
  and stale generations after arena reuse. Assert audio-facing paths never call
  filesystem methods or allocate memory.

Hardware/manual tests:

1. Trigger every zone at note/velocity boundaries and audition mono/stereo
   libraries. Confirm correct MIDI/tracker octave mapping and no unexpected
   layers, envelope, filtering, or looping.
2. Play eight unrelated stereo streams and eight voices sharing one file;
   exercise rapid retriggers, alternating distant zones, ties, pitch commands,
   note-offs, instrument changes, and maximum admitted playback speed.
3. Compare rendered PCM with a host reference for the supported subset. Use
   DecentSampler only where its excluded defaults/DSP can be neutralized;
   exact whole-plugin sound equality is not the acceptance target.
4. Run qualified cards with fragmented files and concurrent allowed browsing,
   preview, recording, project save/export. Record median/tail/max service
   times, time-to-exhaustion, and mutex hold durations, not just MB/s.
5. Inject slow reads/timeouts, short reads, missing files, removal/reinsertion,
   and import cancellation. Other channels and the UI remain responsive;
   deferred diagnostics identify the affected instrument.
6. Repeatedly load/unload/clone/delete with resident samples and almost-full
   SDRAM. Check no overlap, leak, pointer relocation, or stale completion writes.
7. Check fresh Debug/Release maps and worker/main/ISR stack headroom. Run existing
   relevant host suites and Advance builds; keep WASM builds compiling.

Release criteria:

- Triggering requires no synchronous disk operation and adds no delay beyond
  normal audio scheduling when the instrument is ready.
- Zero stream underflows and zero audio deadline misses in a defined sustained
  stress run (initial target: 30 minutes) across qualified cards at the published
  eight-voice/pitch envelope. Outside that envelope, failure is bounded and safe.
- Every loaded instance stays within its reserved maximum of 4,194,304 bytes;
  internal allocations satisfy the refreshed linker budgets and stack margins.
- Record maximum render time against the actual configured audio-block deadline;
  initial target is at least 25% deadline headroom in the stress configuration.
- No unbounded per-note allocations, file opens in rendering, polling in audio,
  or undiagnosed partial preset imports.
- Old project/instrument types retain their IDs and load/play correctly.

## 13. Remaining design decisions to settle during implementation

The user-facing scope questions are answered above. These technical choices
remain explicit gates rather than hidden assumptions:

- Final qualified SD latency envelope, maximum pitch ratio, preload coverage,
  page size and zone limit: derive from Phase 1/3 measurements.
- Whether static DS gain/pan/tuning merit a later small extension; version one
  reports them as unsupported sound settings rather than implying full fidelity.
- Exact filesystem arbitration coverage and whether recording while streaming
  meets measured deadlines. No concurrency promise until tested.
- Whether project export copies external multisample assets or reports them as
  dependencies; external library references are the initial loading proposal.
- Whether D2 hot windows improve measured render/cache behavior enough to keep;
  correctness must work from published SDRAM pages without this optimization.

For the later firmware change, follow the repository issue workflow once an
issue URL/number exists: read it, create `<issue>-short-description`, obtain
approval of the implementation plan, then implement and present hardware
testing instructions. This planning-only request supplies no issue number and
does not authorize firmware implementation.

Suggested commit title for this document: `Plan Advance streamed multisample instrument`

Suggested one-sentence commit description:
`Define DecentSampler subset, streaming design, and validation gates.`
