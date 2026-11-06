/*
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
// Just the RemoteUIProtocol header for now
#include "RemoteUIProtocol.h"

// Helper function for byte escaping.
static uint16_t addByteEscaped(char *buffer, uint16_t bufferIndex,
                               uint8_t byte) {
  if (byte == REMOTE_UI_CMD_MARKER || byte == REMOTE_UI_ESC_CHAR) {
    buffer[bufferIndex++] = REMOTE_UI_ESC_CHAR;
    buffer[bufferIndex++] = byte ^ REMOTE_UI_ESC_XOR;
  } else {
    buffer[bufferIndex++] = byte;
  }
  return bufferIndex;
}

// Helper function to add a 16-bit value, escaping each of its two bytes.
static uint16_t add16bitEscaped(char *buffer, uint16_t bufferIndex,
                                uint16_t val) {
  bufferIndex = addByteEscaped(buffer, bufferIndex, val & 0xFF); // Add LSB
  bufferIndex =
      addByteEscaped(buffer, bufferIndex, (val >> 8) & 0xFF); // Add MSB
  return bufferIndex;
}

void remoteUIDrawCharCommand(const char c, uint8_t x, uint8_t y, bool invert,
                             char *buffer) {
  buffer[0] = REMOTE_UI_CMD_MARKER;
  buffer[1] = TEXT_CMD;
  buffer[2] = c;
  buffer[3] = x + ASCII_SPACE_OFFSET; // to avoid sending NUL (aka 0)
  buffer[4] = y + ASCII_SPACE_OFFSET;
  buffer[5] = invert ? 127 : 0;
}

uint16_t remoteUIDrawRectCommand(int left, int top, int width, int height,
                                 char *buffer) {
  uint16_t bufferIndex = 0;
  buffer[bufferIndex++] = REMOTE_UI_CMD_MARKER;
  buffer[bufferIndex++] = DRAWRECT_CMD;
  bufferIndex = add16bitEscaped(buffer, bufferIndex, left);
  bufferIndex = add16bitEscaped(buffer, bufferIndex, top);
  bufferIndex = add16bitEscaped(buffer, bufferIndex, width);
  bufferIndex = add16bitEscaped(buffer, bufferIndex, height);
  return bufferIndex;
}

void remoteUIClearCommand(unsigned short r, unsigned short g, unsigned short b,
                          char *buffer) {
  buffer[0] = REMOTE_UI_CMD_MARKER;
  buffer[1] = CLEAR_CMD;
  buffer[2] = r;
  buffer[3] = g;
  buffer[4] = b;
}

uint16_t remoteUISetColorCommand(unsigned short r, unsigned short g,
                                 unsigned short b, char *buffer) {
  uint16_t bufferIndex = 0;
  buffer[bufferIndex++] = REMOTE_UI_CMD_MARKER;
  buffer[bufferIndex++] = SETCOLOR_CMD;
  bufferIndex = addByteEscaped(buffer, bufferIndex, r);
  bufferIndex = addByteEscaped(buffer, bufferIndex, g);
  bufferIndex = addByteEscaped(buffer, bufferIndex, b);
  return bufferIndex;
}

static uint16_t addBounds(char *buffer, uint16_t i, GraphicsRegion r) {
  i = add16bitEscaped(buffer, i, r.x);
  i = add16bitEscaped(buffer, i, r.y);
  i = add16bitEscaped(buffer, i, r.width);
  return add16bitEscaped(buffer, i, r.height);
}

uint16_t remoteUIGlyphCommand(uint8_t target, uint8_t font, uint8_t character,
                              bool invert, uint16_t x, uint16_t y,
                              GraphicsRegion clip, bool clipped, char *buffer) {
  buffer[0] = REMOTE_UI_CMD_MARKER;
  buffer[1] = clipped ? CLIPPED_GLYPH_CMD : GLYPH_CMD;
  uint16_t i = addByteEscaped(buffer, 2, target);
  i = addByteEscaped(buffer, i, font);
  i = addByteEscaped(buffer, i, character);
  i = addByteEscaped(buffer, i, invert ? 1 : 0);
  i = add16bitEscaped(buffer, i, x);
  i = add16bitEscaped(buffer, i, y);
  return clipped ? addBounds(buffer, i, clip) : i;
}

uint16_t remoteUITargetRectCommand(uint8_t target, GraphicsRegion bounds,
                                   char *buffer) {
  buffer[0] = REMOTE_UI_CMD_MARKER;
  buffer[1] = TARGET_RECT_CMD;
  uint16_t i = addByteEscaped(buffer, 2, target);
  return addBounds(buffer, i, bounds);
}

uint16_t remoteUIOverlayCommand(RemoteUICommand command, GraphicsRegion bounds,
                                char *buffer) {
  buffer[0] = REMOTE_UI_CMD_MARKER;
  buffer[1] = command;
  return command == OVERLAY_PUSH_CMD ? addBounds(buffer, 2, bounds) : 2;
}
