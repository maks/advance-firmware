#!/usr/bin/env python3
"""Generate Dx7Patches.h from one or more DX7 32-voice bank sysex files.

Usage:
  tools/gen_dx7_patches.py <bank1.syx> [<bank2.syx> ...] \
      sources/Application/Instruments/Dx7Patches.h

The last argument is the output header; every preceding argument is a
32-voice bank sysex file. Each bank is validated (header, size, checksum)
the same way the firmware's ValidateDx7BankSysex does, then all banks'
packed 128-byte voices are concatenated and emitted as a single C array.

Each 32-voice source bank is split into two 16-voice *display* banks so the
preset browser can show a whole bank without scrolling. The built-in banks use
short timbral names chosen from C3 renders made by render_fm6_presets.cpp.
Unknown input bank names fall back to "<Base>1" and "<Base>2".

Voice names carry an uppercase category suffix in the source data
(e.g. "KnappersBA", "JabberORCH"); that suffix is stripped from the emitted
name bytes so the browser shows "Knappers", "Jabber" etc.
"""
import os
import re
import sys

if len(sys.argv) < 3:
    sys.exit(__doc__.strip())

SYX_FILES = sys.argv[1:-1]
OUT = sys.argv[-1]

# Number of voices shown per bank in the preset browser. Must match
# Dx7PresetBankSize in FM6Dx7Patch.h.
PRESET_BANK_SIZE = 16

# A trailing run of >=2 uppercase letters is a DX7 category code, not part of
# the voice name proper.
CAPS_SUFFIX_RE = re.compile(r"[A-Z]{2,}$")

# These labels describe the relative sound of each 16-preset half, based on
# C3 renders from the firmware FM6 engine. Keep them at 12 characters or less
# so the two-column preset browser layout remains aligned.
DISPLAY_BANK_NAMES = {
    "Bass": ["DeepBass", "BrightBass"],
    "Orch": ["WarmOrch", "BrightOrch"],
    "Other": ["DarkFX", "LightFX"],
    "Pads": ["WarmPad", "BrightPad"],
    "Perc": ["HiPerc", "LowPerc"],
    "Pianos": ["BrightKeys", "WarmKeys"],
    "Pluck": ["SoftPluck", "HardPluck"],
    "Synth": ["MellowSynth", "SparkleSynth"],
}
assert all(
    len(name) <= 12 for names in DISPLAY_BANK_NAMES.values() for name in names
)

# Local voice indices in display order for each 32-voice source bank. These
# are balanced 16/16 assignments calculated from every preset's C3 render:
# Spectral centroid separates warm/deep/dark/soft/mellow from
# bright/light/sparkling. The ordering is reversed for the HiPerc and
# BrightKeys categories.
DISPLAY_BANK_ORDERS = {
    "Bass": [
        15, 28, 6, 16, 22, 5, 20, 29, 27, 17, 2, 14, 0, 18, 3, 25,
        7, 19, 21, 26, 30, 8, 31, 13, 4, 12, 11, 10, 9, 1, 24, 23,
    ],
    "Orch": [
        25, 23, 24, 10, 6, 3, 9, 7, 5, 22, 29, 0, 17, 15, 12, 2,
        16, 21, 11, 26, 18, 13, 14, 4, 28, 20, 1, 27, 8, 19, 30, 31,
    ],
    "Other": [
        22, 20, 27, 12, 26, 4, 9, 21, 5, 24, 14, 17, 6, 8, 16, 7,
        30, 18, 15, 10, 11, 23, 3, 13, 1, 28, 29, 31, 25, 0, 2, 19,
    ],
    "Pads": [
        29, 1, 18, 9, 10, 2, 0, 30, 26, 27, 6, 8, 5, 20, 7, 12,
        17, 4, 28, 13, 16, 11, 3, 22, 23, 24, 15, 14, 31, 19, 21, 25,
    ],
    "Perc": [
        12, 28, 18, 15, 6, 24, 4, 19, 7, 16, 5, 2, 14, 0, 10, 27,
        8, 21, 25, 26, 31, 20, 23, 30, 3, 11, 29, 22, 13, 1, 17, 9,
    ],
    "Pianos": [
        8, 29, 12, 28, 11, 3, 1, 23, 15, 9, 30, 31, 26, 27, 10, 16,
        4, 14, 20, 2, 25, 6, 24, 22, 13, 0, 19, 18, 17, 7, 21, 5,
    ],
    "Pluck": [
        16, 7, 12, 24, 4, 28, 20, 8, 14, 2, 22, 27, 6, 5, 15, 18,
        29, 11, 0, 17, 30, 9, 10, 23, 13, 26, 19, 3, 1, 21, 31, 25,
    ],
    "Synth": [
        20, 14, 24, 19, 26, 27, 10, 3, 9, 12, 29, 18, 4, 7, 8, 17,
        5, 22, 31, 15, 28, 11, 21, 30, 25, 1, 0, 2, 23, 13, 6, 16,
    ],
}
assert all(
    sorted(order) == list(range(32)) for order in DISPLAY_BANK_ORDERS.values()
)


def load_bank(path):
    """Validate a 32-voice bank sysex file and return its 4096-byte payload."""
    data = open(path, "rb").read()
    assert len(data) == 4104, f"{path}: bad size {len(data)}"
    assert data[0] == 0xF0 and data[1] == 0x43, f"{path}: bad sysex header"
    assert (data[2] & 0xF0) == 0 and data[3] == 0x09, \
        f"{path}: not a 32-voice bank"
    assert data[4] == 0x20 and data[5] == 0x00, f"{path}: bad byte count"
    assert data[-1] == 0xF7, f"{path}: missing EOX"

    payload = data[6 : 6 + 4096]
    assert all(b < 0x80 for b in payload), \
        f"{path}: payload byte with high bit set"
    checksum = data[6 + 4096]
    expected = (128 - (sum(payload) & 0x7F)) & 0x7F
    assert checksum == expected, \
        f"{path}: checksum mismatch: {checksum:#x} != {expected:#x}"
    return payload


def voice_name(voice):
    return "".join(
        chr(b) if 0x20 <= b <= 0x7E else " " for b in voice[118:128]
    ).rstrip()


def strip_caps_suffix(name):
    """Drop a trailing >=2-char uppercase category code and its separator."""
    stripped = CAPS_SUFFIX_RE.sub("", name.rstrip()).rstrip()
    # Never strip the whole name away (e.g. a genuinely all-caps voice name).
    return stripped or name.rstrip()


def clean_voice_name_bytes(voice):
    """Rewrite bytes 118-127 with the caps-suffix-stripped, space-padded name."""
    cleaned = strip_caps_suffix(voice_name(voice))[:10].ljust(10)
    voice[118:128] = cleaned.encode("ascii", "replace")


def display_bank_names(stem):
    """Return two 16-voice display bank names for a source file stem."""
    base = re.sub(r"[-_ ]?\d+$", "", stem)
    if base in DISPLAY_BANK_NAMES:
        return DISPLAY_BANK_NAMES[base]
    return [f"{base}1", f"{base}2"]


def display_bank_order(stem):
    """Return the local voice permutation that groups the two categories."""
    base = re.sub(r"[-_ ]?\d+$", "", stem)
    return DISPLAY_BANK_ORDERS.get(base, list(range(32)))


# Collect every voice from every bank, in order, splitting each 32-voice source
# bank into two 16-voice display banks.
voices = []
banks = []
bank_names = []
for path in SYX_FILES:
    payload = load_bank(path)
    stem = os.path.splitext(os.path.basename(path))[0]
    bank_voices = [
        bytearray(payload[p * 128 : (p + 1) * 128]) for p in range(32)
    ]
    for voice in bank_voices:
        clean_voice_name_bytes(voice)
    names = display_bank_names(stem)
    order = display_bank_order(stem)
    ordered_voices = [bank_voices[index] for index in order]
    voices.extend(ordered_voices)
    banks.append((names[0], ordered_voices[:PRESET_BANK_SIZE]))
    banks.append((names[1], ordered_voices[PRESET_BANK_SIZE:]))
    bank_names.extend(names)

num_patches = len(voices)
num_banks = len(bank_names)


def c_string_literal(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'

lines = []
lines.append("/**")
lines.append(" * DX7 Patch Data")
lines.append(" * Automatically generated from SYX file(s)")
lines.append(" */")
lines.append("")
lines.append("#ifndef DX7_PATCHES_H")
lines.append("#define DX7_PATCHES_H")
lines.append("")
lines.append("#include <stdint.h>")
lines.append("")
lines.append(f"#define DX7_NUM_PATCHES {num_patches}")
lines.append(f"#define DX7_BANK_SIZE {PRESET_BANK_SIZE}")
lines.append(f"#define DX7_NUM_BANKS {num_banks}")
lines.append("")
lines.append("// Preset browser display bank names, one per DX7_BANK_SIZE-voice")
lines.append("// bank. The bank for patch p is dx7_bank_names[p / DX7_BANK_SIZE].")
lines.append("static const char *const dx7_bank_names[DX7_NUM_BANKS] = {")
for i, name in enumerate(bank_names):
    lines.append(
        f"    {c_string_literal(name)}"
        + ("," if i < num_banks - 1 else "};")
    )
lines.append("")
lines.append("// Each patch is 128 bytes of DX7 parameter data")
lines.append("static const uint8_t dx7_patches[DX7_NUM_PATCHES][128] = {")

for p, voice in enumerate(voices):
    body_lines = []
    for row in range(0, 128, 12):
        chunk = voice[row : row + 12]
        body_lines.append(", ".join(f"0x{b:02X}" for b in chunk))
    entry = f"    {{// Patch {p}\n     " + ",\n     ".join(body_lines) + "}"
    lines.append(entry + ("," if p < num_patches - 1 else "};"))

lines.append("")
lines.append("#endif // DX7_PATCHES_H")
lines.append("")

open(OUT, "w").write("\n".join(lines))

index = 0
for name, bank_voices in banks:
    print(f"# {name}")
    for voice in bank_voices:
        print(f"{index:3d}: {voice_name(voice)}")
        index += 1
