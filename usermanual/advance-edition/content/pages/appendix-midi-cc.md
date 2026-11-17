---
title: MIDI Voice CC Reference
template: page
---

## MIDI Voice CC Reference

Incoming MIDI control changes edit the Voice settings of an instrument. When song channels have MIDI input assignments, the MIDI channel's selected program chooses the instrument. When no song channels have MIDI input assignments, MIDI channels 1–8 control the instrument currently playing on sequencer channels 1–8 respectively; other channels are ignored. Changes affect every voice using that instrument and are saved with the project.

CC values span 0–127 and are scaled to each field's full range. Filter envelope amount is centered at CC value 64. For a field with a small set of choices, the range is divided across those choices. LFO rate controls the sync division when the LFO is in sync mode and the speed otherwise. LFO depth scales to the range of its current target. Selecting a target already used by the other LFO clears the other LFO's target.

| CC | Voice field | CC | Voice field |
|---:|---|---:|---|
| 7 | Amp level | 10 | Pan |
| 14 | Trigger length | 15 | Bandpass base |
| 16 | Bandpass width | 17 | Filter type |
| 18 | Filter envelope amount | 19 | Filter attack |
| 20 | Filter decay | 21 | Filter sustain |
| 22 | Filter release | 23 | Amp sustain |
| 24 | FX1 type | 25 | FX1 amount |
| 26 | FX1 position | 27 | FX2 type |
| 28 | FX2 amount | 29 | FX2 position |
| 30 | LFO1 target | 36 | LFO2 target |
| 31 | LFO1 shape | 37 | LFO2 shape |
| 32 | LFO1 trigger | 38 | LFO2 trigger |
| 33 | LFO1 rate mode | 39 | LFO2 rate mode |
| 34 | LFO1 rate | 40 | LFO2 rate |
| 35 | LFO1 depth | 41 | LFO2 depth |
| 71 | Filter resonance | 72 | Amp release |
| 73 | Amp attack | 74 | Filter cutoff |
| 75 | Amp decay | | |

Changing an insert effect type resets its amount to the effect's default, as it does on the Voice screen. Instruments without the shared Voice screen do not respond to these CCs.
