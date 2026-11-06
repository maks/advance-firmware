/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#include "UIFramework/BasicDatas/TextHighlight.h"
#include "UIFramework/BasicDatas/TextLayoutGaps.h"
#include "UIFramework/BasicDatas/TextRegionLayout.h"
#include "doctest/doctest.h"
#include <array>

TEST_CASE("Highlight corners join by colour and stop at region boundaries") {
  const uint8_t props[] = {0x81, 0x81, 0x82, 0, 0x81, 0x81, 0x82, 0};
  CHECK(TextHighlightCorners(props, 4, 2, 0, 0) == 1);
  CHECK(TextHighlightCorners(props, 4, 2, 1, 0) == 2);
  CHECK(TextHighlightCorners(props, 4, 2, 0, 1) == 4);
  CHECK(TextHighlightCorners(props, 4, 2, 1, 1) == 8);
  CHECK(TextHighlightCorners(props, 4, 2, 2, 0) == 3);
  CHECK(TextHighlightCorners(props, 4, 2, 2, 1) == 12);
  CHECK(TextHighlightCorners(props, 4, 2, 3, 1) == 0);
}

TEST_CASE("Incremental highlight painting matches complete painting as areas "
          "change") {
  constexpr int columns = 6, rows = 4, glyph = 8;
  constexpr int width = columns * glyph, height = rows * glyph;
  std::array<uint8_t, columns * rows> chars{}, previous{}, props{},
      previousProps{};
  std::array<uint8_t, width * height> incremental{}, complete{};
  uint8_t dirty[(columns * rows + 7) / 8];
  auto render = [&](auto &pixels, bool changedOnly) {
    for (int y = 0; y < rows; ++y) {
      for (int x = 0; x < columns; ++x) {
        const int at = y * columns + x;
        if (changedOnly && !(dirty[at / 8] & (1U << (at % 8))))
          continue;
        for (int py = 0; py < glyph; ++py)
          for (int px = 0; px < glyph; ++px)
            pixels[(y * glyph + py) * width + x * glyph + px] = props[at];
        TextHighlightPaint(
            TextHighlightCorners(props.data(), columns, rows, x, y), x * glyph,
            y * glyph, glyph, glyph, [&](int px, int py, int w, int h) {
              for (int yy = py; yy < py + h; ++yy)
                for (int xx = px; xx < px + w; ++xx)
                  pixels[yy * width + xx] = 0;
            });
      }
    }
  };
  auto step = [&] {
    TextHighlightChanges(chars.data(), props.data(), previous.data(),
                         previousProps.data(), columns, rows, dirty);
    render(incremental, true);
    render(complete, false);
    CHECK(incremental == complete);
    previous = chars;
    previousProps = props;
  };
  // Every rectangle, in different positions and with both growth and shrink.
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < columns; ++x)
      for (int h = 1; h <= rows - y; ++h)
        for (int w = 1; w <= columns - x; ++w) {
          props.fill(0);
          for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
              props[yy * columns + xx] = 0x81;
          step();
        }
  // Split, merge, recolour, concave edges, and rapid edits across byte edges.
  uint32_t seed = 0x37251;
  for (int i = 0; i < 1000; ++i) {
    seed = seed * 1664525 + 1013904223;
    const int at = (seed >> 8) % props.size();
    props[at] = (seed & 1) ? uint8_t(0x81 + ((seed >> 1) & 1)) : 0;
    ++chars[at];
    step();
  }
  props.fill(0);
  step();
  step();
  for (uint8_t bits : dirty)
    CHECK(bits == 0);
}

TEST_CASE("Rounded corner mask touches only the outer three pixels") {
  int touched = 0;
  TextHighlightPaint(15, 10, 20, 16, 24, [&](int x, int y, int w, int h) {
    CHECK(x >= 10);
    CHECK(x + w <= 26);
    CHECK(y >= 20);
    CHECK(y + h <= 44);
    touched += w * h;
  });
  CHECK(touched == 12);
}

TEST_CASE(
    "Selection styling includes separators and restores content on shrink") {
  TextRegionInfo region{};
  region.offset = 3;
  region.columns = 8;
  region.rows = 3;
  std::array<uint8_t, 30> props{}, previousProps{}, chars{}, previous{};
  // Distinct normal colours represent beat emphasis and field colouring.
  for (size_t i = 0; i < props.size(); ++i) {
    props[i] = 1 + i % 3;
    chars[i] = i % 3 ? 'A' : ' ';
  }
  const auto normal = props;
  const auto originalChars = chars;
  text_region_style(&region, props.data(), 1, 0, 7, 3, 0x82);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 8; ++x) {
      const int at = region.offset + y * 8 + x;
      CHECK(props[at] == (x >= 1 && x < 7 ? 0x82 : normal[at]));
    }
  CHECK(chars == originalChars);
  // Playback updates a separator glyph with its normal colour. Reapplying
  // selection properties retains the marker while restoring continuous fill.
  chars[3 + 3] = '>';
  props[3 + 3] = 3;
  text_region_style(&region, props.data(), 1, 0, 7, 3, 0x82);
  CHECK(chars[3 + 3] == '>');
  CHECK(props[3 + 3] == 0x82);
  CHECK(props[2] == normal[2]);
  CHECK(props[27] == normal[27]);
  previousProps = props;
  previous = chars;
  // Exactly the view lifecycle: rebuild normal properties, then style the
  // smaller selection. Former selected cells recover their individual colours.
  props = normal;
  text_region_style(&region, props.data(), 2, 1, 5, 2, 0x82);
  uint8_t dirty[3];
  TextHighlightChanges(chars.data() + 3, props.data() + 3, previous.data() + 3,
                       previousProps.data() + 3, 8, 3, dirty);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 8; ++x) {
      const int at = y * 8 + x;
      const bool selected = y == 1 && x >= 2 && x < 5;
      CHECK(props[3 + at] == (selected ? 0x82 : normal[3 + at]));
      if (props[3 + at] != previousProps[3 + at])
        CHECK((dirty[at / 8] & (1U << (at % 8))) != 0);
    }
}

TEST_CASE(
    "Selection styling clips visible bounds and ignores empty rectangles") {
  TextRegionInfo region{};
  region.offset = 2;
  region.columns = 4;
  region.rows = 3;
  std::array<uint8_t, 16> props{};
  text_region_style(&region, props.data(), -2, -10, 2, 2, 0x81);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 4; ++x)
      CHECK(props[2 + y * 4 + x] == (x < 2 && y < 2 ? 0x81 : 0));
  const auto clipped = props;
  text_region_style(&region, props.data(), 3, 1, 2, 3, 0x82);
  text_region_style(&region, props.data(), 0, 4, 4, 8, 0x82);
  CHECK(props == clipped);
  text_region_style(&region, props.data(), 0, 0, 100, 100, 0x82);
  CHECK(props[0] == 0);
  CHECK(props[1] == 0);
  CHECK(props[14] == 0);
  CHECK(props[15] == 0);
  for (int i = 2; i < 14; ++i)
    CHECK(props[i] == 0x82);
}

TEST_CASE("Layout gap rectangles cover exactly the pixels outside glyphs and "
          "graphs") {
  constexpr int width = 80, height = 60;
  static uint8_t writes[width * height];
  TextRegionLayout text{};
  GraphicsRegion graphs[GRAPHICS_REGION_LIMIT]{};
  uint32_t seed = 0x25348;
  auto next = [&] {
    seed = seed * 1664525U + 1013904223U;
    return seed;
  };
  // Includes empty layouts, overlapping coverage, region offsets, and strips
  // too small for complete glyphs. Compare emitted rectangles to pixel truth.
  for (int trial = 0; trial < 100; ++trial) {
    text.count = trial % (TEXT_REGION_LIMIT + 1);
    const uint8_t graphCount = trial % (GRAPHICS_REGION_LIMIT + 1);
    for (uint8_t i = 0; i < text.count; ++i) {
      auto &r = text.regions[i];
      r.definition = {uint16_t(next() % width), uint16_t(next() % height), 0, 0,
                      0};
      r.glyph_width = 3;
      r.glyph_height = 5;
      r.definition.width = 1 + next() % (width - r.definition.x);
      r.definition.height = 1 + next() % (height - r.definition.y);
      r.columns = r.definition.width / r.glyph_width;
      r.rows = r.definition.height / r.glyph_height;
    }
    for (uint8_t i = 0; i < graphCount; ++i) {
      const int16_t x = next() % width, y = next() % height;
      graphs[i] = {x, y, int16_t(1 + next() % (width - x)),
                   int16_t(1 + next() % (height - y))};
    }
    memset(writes, 0, sizeof(writes));
    PaintTextLayoutGaps(text, {graphs, graphCount}, width, height,
                        [&](GraphicsRegion r) {
                          REQUIRE(r.x >= 0);
                          REQUIRE(r.y >= 0);
                          REQUIRE(r.width > 0);
                          REQUIRE(r.height > 0);
                          REQUIRE(r.x + r.width <= width);
                          REQUIRE(r.y + r.height <= height);
                          for (int y = r.y; y < r.y + r.height; ++y)
                            for (int x = r.x; x < r.x + r.width; ++x)
                              ++writes[y * width + x];
                        });
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        bool covered = false;
        for (uint8_t i = 0; i < text.count; ++i) {
          const auto &r = text.regions[i];
          covered |= x >= r.definition.x && y >= r.definition.y &&
                     x < r.definition.x + r.columns * r.glyph_width &&
                     y < r.definition.y + r.rows * r.glyph_height;
        }
        for (uint8_t i = 0; i < graphCount; ++i) {
          const auto &r = graphs[i];
          covered |=
              x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
        }
        REQUIRE(writes[y * width + x] == (covered ? 0 : 1));
      }
    }
  }
}

TEST_CASE(
    "Bounded highlight scans retain corner dependencies outside the bounds") {
  const uint8_t chars[] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  uint8_t props[9] = {}, previousProps[9] = {};
  props[4] = previousProps[4] = 0x81;
  uint8_t dirty[2];
  for (uint16_t neighbour : {1, 3, 5, 7}) {
    props[neighbour] = 0x81;
    TextHighlightChanges(chars, props, chars, previousProps, 3, 3, dirty, 1, 1,
                         2, 2);
    CHECK(dirty[0] == (1U << 4));
    CHECK(dirty[1] == 0);
    // Removing the neighbour must dirty the same cell to restore its corners.
    TextHighlightChanges(chars, previousProps, chars, props, 3, 3, dirty, 1, 1,
                         2, 2);
    CHECK(dirty[0] == (1U << 4));
    CHECK(dirty[1] == 0);
    props[neighbour] = 0;
  }
}

TEST_CASE("Bounded highlight scans match full scans as visible bounds change") {
  constexpr uint16_t columns = 32, rows = 24, cells = columns * rows;
  static uint8_t chars[cells], previous[cells], props[cells],
      previousProps[cells];
  uint8_t full[cells / 8], bounded[cells / 8];
  memset(chars, ' ', sizeof(chars));
  memset(props, 0, sizeof(props));
  // New overlays invalidate the entire previous cache, including hidden cells.
  memset(previous, 0xff, sizeof(previous));
  memset(previousProps, 0xff, sizeof(previousProps));
  uint32_t seed = 0x46251;
  auto next = [&] {
    seed = seed * 1664525U + 1013904223U;
    return seed;
  };
  for (uint16_t step = 0; step < 200; ++step) {
    for (uint16_t edit = 0; edit < 12; ++edit) {
      const uint16_t at = next() % cells;
      chars[at] = 32 + next() % 224;
      props[at] = (next() & 1) ? 0x81 : 0;
    }
    uint16_t left = next() % columns, top = next() % rows;
    uint16_t right = left + next() % (columns - left + 1);
    uint16_t bottom = top + next() % (rows - top + 1);
    if (step % 10 == 0) {
      left = top = 0;
      right = columns;
      bottom = rows;
    }
    TextHighlightChanges(chars, props, previous, previousProps, columns, rows,
                         full);
    TextHighlightChanges(chars, props, previous, previousProps, columns, rows,
                         bounded, left, top, right, bottom);
    for (uint16_t y = 0; y < rows; ++y) {
      for (uint16_t x = 0; x < columns; ++x) {
        const uint16_t at = y * columns + x;
        const uint8_t bit = 1U << (at % 8);
        const bool visible = x >= left && x < right && y >= top && y < bottom;
        CHECK(bool(bounded[at / 8] & bit) ==
              (visible && bool(full[at / 8] & bit)));
        // Flush updates only cells it actually rendered. Later expansion must
        // still discover changes outside the previous visible rectangle.
        if (bounded[at / 8] & bit) {
          previous[at] = chars[at];
          previousProps[at] = props[at];
        }
      }
    }
  }
}
