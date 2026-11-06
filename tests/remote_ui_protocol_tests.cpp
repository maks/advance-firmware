/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#include "System/RemoteUI/RemoteUIProtocol.h"
#include "UIFramework/BasicDatas/TextRegionLayout.h"
#include "doctest/doctest.h"
#include <cstring>
#include <initializer_list>

namespace {
struct Packet {
  uint8_t payload[32]{};
  uint16_t size = 0;
};
Packet decode(const char *encoded, uint16_t length, RemoteUICommand command) {
  REQUIRE(uint8_t(encoded[0]) == REMOTE_UI_CMD_MARKER);
  REQUIRE(uint8_t(encoded[1]) == command);
  Packet result;
  for (uint16_t i = 2; i < length; ++i) {
    uint8_t byte = encoded[i];
    REQUIRE(byte != REMOTE_UI_CMD_MARKER);
    if (byte == REMOTE_UI_ESC_CHAR) {
      REQUIRE(++i < length);
      byte = uint8_t(encoded[i]) ^ REMOTE_UI_ESC_XOR;
    }
    REQUIRE(result.size < sizeof(result.payload));
    result.payload[result.size++] = byte;
  }
  return result;
}
uint16_t word(const Packet &packet, uint16_t at) {
  return packet.payload[at] | (uint16_t(packet.payload[at + 1]) << 8);
}
} // namespace

TEST_CASE(
    "Native remote glyph packets preserve identity coordinates and clipping") {
  char buffer[35];
  for (uint16_t c = 32; c <= 255; ++c) {
    memset(buffer, 0x55, sizeof(buffer));
    const GraphicsRegion clip{254, 253, 16, 24};
    uint16_t length = remoteUIGlyphCommand(1, TEXT_FONT_SMALL, c, true, 253,
                                           254, clip, true, buffer);
    CHECK(length <= 34);
    CHECK(buffer[34] == 0x55);
    const Packet p = decode(buffer, length, CLIPPED_GLYPH_CMD);
    REQUIRE(p.size == 16);
    CHECK(p.payload[0] == 1);
    CHECK(p.payload[1] == TEXT_FONT_SMALL);
    CHECK(p.payload[2] == c);
    CHECK(p.payload[3] == 1);
    CHECK(word(p, 4) == 253);
    CHECK(word(p, 6) == 254);
    CHECK(word(p, 8) == 254);
    CHECK(word(p, 10) == 253);
    CHECK(word(p, 12) == 16);
    CHECK(word(p, 14) == 24);
  }
}

TEST_CASE(
    "Remote overlay commands and target rectangles round trip escaped bounds") {
  const GraphicsRegion bounds{253, 254, 254, 253};
  char buffer[20];
  auto p = decode(buffer, remoteUITargetRectCommand(2, bounds, buffer),
                  TARGET_RECT_CMD);
  REQUIRE(p.size == 9);
  CHECK(p.payload[0] == 2);
  CHECK(word(p, 1) == 253);
  CHECK(word(p, 3) == 254);
  CHECK(word(p, 5) == 254);
  CHECK(word(p, 7) == 253);
  p = decode(buffer, remoteUIOverlayCommand(OVERLAY_PUSH_CMD, bounds, buffer),
             OVERLAY_PUSH_CMD);
  REQUIRE(p.size == 8);
  CHECK(word(p, 0) == 253);
  CHECK(word(p, 2) == 254);
  CHECK(word(p, 4) == 254);
  CHECK(word(p, 6) == 253);
  for (auto command : {OVERLAY_POP_CMD, OVERLAY_RESET_CMD}) {
    p = decode(buffer, remoteUIOverlayCommand(command, bounds, buffer),
               command);
    CHECK(p.size == 0);
  }
  p = decode(buffer, remoteUISetColorCommand(253, 254, 255, buffer),
             SETCOLOR_CMD);
  REQUIRE(p.size == 3);
  CHECK(p.payload[0] == 253);
  CHECK(p.payload[1] == 254);
  CHECK(p.payload[2] == 255);
}

TEST_CASE("Whole remote glyphs omit unnecessary clipping fields") {
  char buffer[18];
  uint16_t length = remoteUIGlyphCommand(0, TEXT_FONT_SMALL, 'M', false, 8, 600,
                                         {8, 600, 16, 24}, false, buffer);
  CHECK(length == 10);
  auto p = decode(buffer, length, GLYPH_CMD);
  REQUIRE(p.size == 8);
  CHECK(p.payload[1] == TEXT_FONT_SMALL);
  CHECK(p.payload[2] == 'M');
  CHECK(word(p, 4) == 8);
  CHECK(word(p, 6) == 600);
}
