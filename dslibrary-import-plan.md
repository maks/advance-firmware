# Decent Sampler library import implementation plan

Status: implementation complete; automated validation passed; representative
library and Advance hardware validation remain pending.

## Goal

Support importing `.dslibrary` files by extracting their ZIP contents into a
child directory beside the archive, then passing a selected extracted
`.dspreset` to the existing Decent Sampler importer.

Archive support does not expand the existing preset or playback feature set.
Extracted presets remain subject to the current importer's validation and
audio conversion rules.

## Approved behavior

### Browser and sequencer

- Show `.dslibrary` files alongside `.dspreset` files when importing an
  MSAMPLER instrument. Match extensions without regard to case.
- Before starting a library import, check whether the sequencer is running.
  If it is, display this exact message and return without starting extraction:

  `cannot import dslibrary while sequencer running`

- The user must explicitly stop playback and retry. Do not stop playback
  automatically or resume a deferred import automatically.
- Prevent sequencer playback from starting while extraction is active, including
  through external transport commands. Release this restriction on completion,
  cancellation, or failure. Do not restart playback afterward.
- Keep the UI responsive using a background extraction worker and progress
  messages. Display the stage, extracted bytes, and current filename. Support
  cancellation during inspection and extraction.

### Destination and existing directories

For example:

```text
Instruments/
  Piano.dslibrary
  Piano/
    Piano.dspreset
    Samples/
      C3.wav
      D3.wav
```

- Derive the destination by removing the `.dslibrary` extension from the archive
  filename, retaining its parent directory.
- If anything already exists at the destination path, do nothing: do not extract,
  merge, overwrite, create a numbered alternative, or automatically import from
  that directory. Report that the destination already exists. The user can browse
  an existing extracted directory separately.
- Preserve the entire archive hierarchy, including any top level folder inside
  the archive, so relative sample paths resolve correctly.
- Extract all regular files, including artwork and documentation. Do not extract
  nested archives recursively.
- Retain the completed extracted directory permanently as reusable source
  material, including when subsequent preset selection is cancelled or preset
  import fails. Leave the original archive untouched.

### Preset discovery and import

- Recursively find `.dspreset` files after extraction succeeds.
- One preset: pass its full path directly to the existing importer.
- Multiple presets: present a chooser showing paths relative to the extraction
  root, then import the selected preset.
- No presets: report `No presets found` and retain the completed extraction.
- Use bounded enumeration and paged presentation. Do not silently truncate a
  library because a directory exceeds the browser's listing capacity.
- Reuse `buildImportedDecentSampler()` in
  `sources/Application/Views/InstrumentImportView.cpp` for validation, conversion,
  and creation of project assets.
- Distinguish extraction failures from unsupported preset or audio content.
  Successful extraction does not imply preset compatibility.

## Archive implementation

Add a dedicated archive extraction component outside the view. It owns archive
inspection, path validation, extraction, integrity checks, progress, cancellation,
and cleanup. Adapt it to the existing `FileSystem` and `FileHandle` abstractions.

Vendor a pinned release of miniz, retain its license notices, and compile only
the reader and decompression functionality needed. Use callback or iterative
extraction instead of whole-file extraction to memory. Disable unused writer,
compression, and standard file I/O functionality where supported.

References:

- Decent Samples describes `.dslibrary` as ZIP archives:
  <https://www.decentsamples.com/tag/decent-sampler/>
- miniz source and licensing: <https://github.com/richgel999/miniz>
- miniz extraction APIs:
  <https://github.com/richgel999/miniz/blob/master/miniz_zip.h>

### Initial ZIP support

- Single volume, ordinary ZIP archives.
- Stored and DEFLATE entries, including entries using data descriptors.
- Nested directories and multiple presets.
- Validate archive structure, extracted sizes, and CRCs before declaring success.
- Reject ZIP64, encrypted archives, split archives, and unsupported compression
  methods with specific errors.
- Check representative real libraries against this supported subset.

### Resource limits

| Resource | Approved limit |
| --- | --- |
| Archive entries, including directories | 4,096 |
| Directory depth below extraction root | 16 levels |
| Individual extracted file | Less than 4 GiB; enforce smaller filesystem limits |
| Total extracted file data | 8 GiB |
| Extraction workspace in external SRAM/SDRAM | Up to 1 MiB |
| Read/write chunk size | 16 KiB |
| Minimum free SD space before extraction | 256 MiB |

Use checked 64 bit arithmetic for archive sizes, offsets, totals, and space
calculations. Enforce existing filesystem filename and full path limits,
including the extra staging path length.

The external memory budget includes archive metadata, decompressor allocations,
and I/O buffers. Use bounded allocation and fail cleanly on exhaustion. Account
for task stack and internal memory separately; verify placement and total usage
on hardware. Never load the archive or a whole sample into RAM.

Before extraction, inspect entries to calculate total output size and reject
unsupported structures. Require both at least 256 MiB of free SD space and enough
space for the calculated output, filesystem allocation overhead, and staging
metadata. The 256 MiB is a minimum starting free-space threshold, not an extra
256 MiB reserve on top of the output size. Replace the earlier proposed 16 MiB
headroom rule with this requirement.

Expose free-space information through the filesystem abstraction if necessary,
with an Advance implementation and appropriate host support. Fail clearly if
free space cannot be established. Recheck capacity before project asset copying
or conversion because that phase can need additional space. Handle write errors
even after successful preflight checks.

### Path validation

Before creating output, normalize archive separators and verify that every
resolved path remains inside the staging root.

Reject:

- Absolute paths, drive-qualified paths, and parent traversal outside the root.
- Unsupported filenames, overlong names or paths, and excessive nesting.
- Symlinks and other unsupported special entries.
- Duplicate output paths, file/directory conflicts, and case insensitive
  collisions on the SD filesystem.
- Metadata or integer overflow and entries exceeding resource limits.

Do not truncate filenames or silently skip invalid entries. Treat archive
contents as untrusted input and report the offending entry where practical.

## Completion, cancellation, and recovery

1. Check sequencer state and destination existence before creating any output.
2. Inspect and validate the archive and available SD capacity.
3. Create a unique staging directory beside the destination with an incomplete
   marker. Staging names are temporary and are never alternative final names.
4. Extract in bounded chunks, checking cancellation between chunks and during
   lengthy inspection work. Coordinate filesystem access with existing workers.
5. Verify integrity and close all output handles.
6. Recheck destination existence and publish the staging directory under the
   exact final name using a directory rename operation. Never replace an
   existing destination. Add a filesystem directory rename API if required.
7. Discover presets and proceed through the existing importer.

On cancellation or extraction failure, close handles and remove only files and
directories created by this extraction operation. Keep the source archive and
unrelated files untouched. If cleanup fails, retain enough ownership metadata
to identify the incomplete staging directory and report the failure.

If a reboot leaves staging output behind, detect it when the relevant browser
directory is entered and offer cleanup or a fresh retry. Do not resume partially
extracted files or treat them as an installed library. Cleanup must be confined
to directories positively identified as owned staging output.

Confirm directory rename and marker ordering against the actual filesystem's
failure behavior. This is a recoverable staging protocol, not a promise of
power-loss atomicity.

## Implementation sequence

1. **Filesystem and extraction foundation:** free-space reporting, directory
   publication support, pinned miniz dependency, bounded workspace, archive
   inspection, path checks, extraction, integrity validation, and cleanup.
2. **Import integration:** browser filtering, explicit running-sequencer error,
   playback-start restriction, worker lifecycle, progress/cancellation, existing
   destination handling, recursive preset chooser, and existing importer handoff.
3. **Validation and documentation:** focused automated coverage, Advance builds,
   hardware checks, and user manual instructions describing extraction location,
   resource limits, stopping playback, and browsing already extracted libraries.

Follow repository C++ conventions, fixed width integer types, bounded ETL strings,
Trace logging, nanoprintf formatting where needed, and clang-format.

## Validation and acceptance

Focused automated checks should cover:

- Stored/DEFLATE entries, data descriptors, nested directories, and multiple
  presets with shared sample directories.
- CRC failures, truncation, malformed metadata, unsupported ZIP features, and
  every configured resource boundary.
- Traversal, absolute paths, special entries, duplicate names, case collisions,
  and file/directory conflicts.
- Insufficient free space, allocation failure, read/write failures, cancellation,
  cleanup failure, and interrupted extraction recovery.
- Existing destination: no extracted files, numbered directories, overwritten
  content, or automatic import.
- Running sequencer: exact required message, playback continues, and extraction
  does not start. Playback-start requests are blocked during active extraction.
- Zero, one, and multiple presets; successful extraction followed by unsupported
  preset content; cancellation after successful extraction.
- Successful project import and save/reload using extracted source assets.

On Advance hardware, verify workspace and stack usage, responsive progress,
cancellation latency, large directory enumeration, free-space handling, extraction
of a library larger than RAM, and persistence of completed and interrupted output
across reboot. Confirm the final directory remains usable through ordinary
`.dspreset` import and the original archive is unchanged.
