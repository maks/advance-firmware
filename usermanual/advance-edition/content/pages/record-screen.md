---
<!--
SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only

Copyright (c) 2026 xiphonics, inc.

Inherited BSD-licensed material remains BSD-3-Clause.
New original contributions by xiphonics, inc. are GPL-3.0-only.
Previously granted BSD permissions are preserved. See LICENSING.md.
-->


title: Record Screen
template: page
---

![record screen screenshot](image/record-screen-small.png)

## Introduction

The Record screen allows you to capture audio from external sources directly into the picoTracker. You can record from a line-level source or the built-in microphone, making it easy to sample sounds from your environment or other gear.

## Accessing the Record Screen

You can access it from any screen by pressing <span class="minikeys">EDIT</span> + <span class="minikeys">PLAY</span> when the sequencer is stopped.

## Screen Layout

The Record screen displays the following information:

*   **Recording Status:** `[REC]` is shown in red when recording is active, and `[---]` is shown otherwise.
*   **Recording Timer:** A timer displays the duration of the current recording in `MM:SS` format.
*   **VU Meter:** A VU meter on the right of the screen shows the input audio level.

## Recording Controls

*   **PLAY:** Press the <span class="minikeys">PLAY</span> button to start recording. If a recording is in progress, pressing <span class="minikeys">PLAY</span> will stop it.
*   **<span class="minikeys">NAV</span> + <span class="minikeys">LEFT</span>:** Press <span class="minikeys">NAV</span> + <span class="minikeys">LEFT</span> to exit the Record screen and return to the Song view. If a recording is in progress, it will be discarded.

## Recording Settings

You can configure the following settings on the Record screen:

*   **Audio source:** Choose between `Line In` and `Mic`.
*   **Line gain:** Adjust the input gain for the line-in source.
*   **Mic gain:** Adjust the input gain for the microphone.

**Note:** The Line In jack requires a **TRS** (tip-ring-sleeve, 3-pole) plug. **TRRS** (4-pole, headset style) plugs are not supported — if your audio source has a TRRS connector, use a passive TRRS to TRS adapter.

Use the <span class="minikeys">ARROW</span> keys to navigate between these settings and <span class="minikeys">EDIT</span> + <span class="minikeys">LEFT</span>/<span class="minikeys">RIGHT</span> to change their values.

## The Recording Process

1.  Navigate to the Record screen.
2.  Select your desired audio source and adjust the gain.
3.  Press <span class="minikeys">PLAY</span> to start recording. The status indicator will turn to `[REC]`, and the timer will start.
4.  When you are finished, press <span class="minikeys">PLAY</span> again to stop.
5.  The picoTracker will save the recording as `REC01.wav` in the `/recordings` directory on your SD card.
6.  After saving, the picoTracker will automatically switch to the Sample Editor screen with the new recording loaded, ready for you to edit and use.

## Tips

*   Keep an eye on the VU meter to avoid clipping (when the meter hits the red) and also to have a high enough signal. Adjust the gain accordingly for a clean recording.
*   If you get no signal at all on the VU meter when using `Line In`, check that your cable has a TRS (3-pole) plug — TRRS (4-pole) headset plugs will not work.
*   The recorded sample will replace any existing `REC01.wav` file in the `/recordings` directory, so be sure to rename your recordings in the Sample Editor if you want to keep them.
