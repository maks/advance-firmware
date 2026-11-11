---
title: picoTracker Remote User Interface Protocol
template: page
---

## Overview

The Remote UI protocol is a communication mechanism that allows rendering the picoTracker's user interface over a USB serial connection. It enables a remote client to:

* Receive UI rendering commands
* (NOT YET IMPLEMENTED) Send button input events back to the device

## Command Structure

### Command Marker

Every command starts with a fixed marker: `0xFE` (REMOTE_UI_CMD_MARKER). This allows clients to verify the start of a valid command.

**Note:** The use of this value as a marker means that if the value `0xFE` or the escape character `0xFD` needs to be sent as part of a command's parameters, they must be escaped. See the **Character Escaping** section below.


### Command Types

Note: `ASCII_SPACE_OFFSET = 0x0F` (15)


1. `TEXT_CMD (0x02)`: Draw a character

Parameters:

* Character to draw
* X position (offset by ASCII_SPACE_OFFSET)
* Y position (offset by ASCII_SPACE_OFFSET)
* Invert flag (0 for normal, 0x7F for inverted)

2. `CLEAR_CMD (0x03)`: Clear screen

Parameters:

* Background color Red (`0x00`-`0xFF`)
* Background color Green (`0x00`-`0xFF`)
* Background color Blue (`0x00`-`0xFF`)

3. `SETCOLOR_CMD (0x04)`: Set foreground color

Parameters:

* Red (`0x00`-`0xFF`, escaped)
* Green (`0x00`-`0xFF`, escaped)
* Blue (`0x00`-`0xFF`, escaped)

Command `0x05` is reserved for the obsolete global font setting and is no longer sent.

4. `DRAWRECT_CMD (0x06)`: Draw a rectangle (can be used for pixel drawing)

Parameters:

* Left (16-bit, Little Endian, escaped)
* Top (16-bit, Little Endian, escaped)
* Width (16-bit, Little Endian, escaped)
* Height (16-bit, Little Endian, escaped)


### Fonts and region text

Advance sends text with `GLYPH_CMD (0x07)` or `CLIPPED_GLYPH_CMD (0x0C)`.
Each command identifies its font directly; clients must have the matching font
assets. Text is never transmitted as pixel data.

Both commands contain these escaped parameters, in order:

* Target (one byte; 0 is the base screen, 1 or 2 is an overlay)
* Font ID (one byte)
* Character code (one byte)
* Invert flag (one byte, 0 or 1)
* X and Y pixel coordinates (each 16-bit, little endian)

`CLIPPED_GLYPH_CMD` then adds the clip rectangle's left, top, width and height,
each as an escaped 16-bit little-endian value.

| Font ID | Asset | Glyph size |
| --- | --- | --- |
| 0 | FONT | 22 × 30 pixels |
| 1 | FONTSMALL | 16 × 24 pixels |
| 2 | FONTTINY | 10 × 15 pixels |

The title bar is 704 pixels wide, fitting 44 small-font characters. Font IDs
refer to fixed assets, not a user-selected global style. The older `TEXT_CMD`
format above is a legacy command; current Advance region text uses glyph commands.

### Character Escaping

To allow the full range of byte values (`0x00`-`0xFF`) in parameters while reserving `0xFE` as a command marker, an escaping mechanism is used for certain commands (like `SETCOLOR_CMD` and `DRAWRECT_CMD`).

* Escape Character: `0xFD`
* Escape XOR mask: `0x20`

If a parameter byte is either `0xFE` or `0xFD`, it is replaced by a two-byte sequence:
1. The escape character `0xFD`
2. The original byte XORed with `0x20`

For example:
* `0xFE` becomes `0xFD 0xDE`
* `0xFD` becomes `0xFD 0xDD`

### Example Transmission Flow

To give a concrete example, below is a simple example of how a command in the picoTracker firmware is transmitted over USB serial:

```cpp

// Drawing a character 'A' at position (2,3), not inverted
// ASCII_SPACE_OFFSET = 0x0F (15)
remoteUIBuffer[0] = 0xFE;        // Command marker
remoteUIBuffer[1] = TEXT_CMD;    // Draw character command (0x02)
remoteUIBuffer[2] = 'A';         // Character
remoteUIBuffer[3] = 0x11;        // X position (2 + 15 = 17)
remoteUIBuffer[4] = 0x12;        // Y position (3 + 15 = 18)
remoteUIBuffer[5] = 0x00;        // Not inverted
sendToUSBCDC(remoteUIBuffer, 6);
```

### Input Commands

Clients are also able to send input events to the picoTracker. As for output commands, the input commands are prefixed by sending the REMOTE_UI_CMD_MARKER byte.

The events currently supported are:

1. `FULL_REFRESH_CMD (0x02)`: Request a full screen refresh; text commands include their font IDs

Parameters:

* None


### Limitations

* Input events other than FULL_REFRESH_CMD are not yet implemented.

### Client Implementation Guidelines

1. Look for REMOTE_UI_CMD_MARKER (`0xFE`)
2. If the next byte is part of a parameter for a command that uses escaping, check if it's the escape character (`0xFD`).
3. Handle potential transmission errors
4. Implement appropriate rendering based on received commands