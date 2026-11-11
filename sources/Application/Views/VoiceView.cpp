/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "VoiceView.h"

#include "Application/AppWindow.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#include "Application/Instruments/VoiceParameters.h"
#include "Application/Model/Config.h"
#include "Application/Model/Project.h"
#include "Application/Player/InsertEffectDefinitions.h"
#include "Application/Player/PlayButtonBehavior.h"
#include "Application/Player/Player.h"
#include "Application/Views/VoiceEnvelopeGraph.h"
#include "Application/Views/VoiceFilterGraph.h"
#include "Application/Views/VoiceScreenUtils.h"
#include "Externals/etl/include/etl/to_string.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <nanoprintf.h>

namespace {
constexpr int32_t EnvelopeGraphY = CHAR_HEIGHT + 8;
constexpr int32_t EnvelopeGraphHeight = 4 * CHAR_HEIGHT;
constexpr int32_t EnvelopeScreenWidth = 720;
constexpr int32_t EnvelopePanelRightMargin = 10;
constexpr int32_t EnvelopePanelWidth =
    EnvelopeScreenWidth - (10 * CHAR_WIDTH) - EnvelopePanelRightMargin;
constexpr int32_t EnvelopePanelX =
    (EnvelopeScreenWidth - EnvelopePanelWidth) / 2;
constexpr int32_t EnvelopePaddingX = 16;
constexpr int32_t EnvelopePaddingTop = 18;
constexpr int32_t EnvelopePaddingBottom = 16;
constexpr int32_t EnvelopeFrameThickness = 1;
constexpr int32_t EnvelopeGuideLineThickness = 1;
constexpr int32_t EnvelopeGuideLineSegmentHeight = 3;
constexpr int32_t EnvelopeGuideLineStep = 6;
constexpr int32_t EnvelopeLineChunkSize = 4;
constexpr int32_t EnvelopeNodeSize = 9;
constexpr int32_t EnvelopeLineStep = 3;
constexpr int32_t InsertModuleWidth = 56;
constexpr int32_t InsertModuleHeight = 28;
constexpr int32_t VoiceColumnX = 1;
constexpr int32_t FilterColumnX = 11;
constexpr int32_t AmpColumnX = 22;
constexpr float LFOGraphPi = 3.14159265358979323846f;

struct InsertGraphSlot {
  int32_t type = kDefaultVoiceInsertEffectIndex;
  int32_t amount = kDefaultVoiceInsertAmount;
  int32_t position = kDefaultVoiceInsertPosition;
};

struct InsertBlockPosition {
  int32_t slot = 0;
  int32_t x = 0;
};

enum class PanLabel : uint8_t { Left, Center, Right };
Variable *findVoiceVariable(InstrumentBank *bank, I_Instrument *instrument,
                            int instrumentID, FourCC id) {
  if (!bank || !instrument) {
    return nullptr;
  }

  if (InstrumentTypeUsesModernAudioVoice(instrument->GetType())) {
    VoiceParameters *voiceParameters = bank->GetVoiceParameters(instrumentID);
    return voiceParameters ? voiceParameters->FindVariable(id) : nullptr;
  }

  return instrument->FindVariable(id);
}

int32_t voiceIntValue(InstrumentBank *bank, I_Instrument *instrument,
                      int32_t instrumentID, FourCC id, int32_t defaultValue,
                      int32_t minValue, int32_t maxValue) {
  Variable *variable = findVoiceVariable(bank, instrument, instrumentID, id);
  if (!variable) {
    return defaultValue;
  }
  return std::clamp<int32_t>(variable->GetInt(), minValue, maxValue);
}
void drawBlock(View &view, int32_t x, int32_t y, int32_t size,
               ColorDefinition color) {
  const int32_t halfSize = size / 2;
  GUIRect block(x - halfSize, y - halfSize, x - halfSize + size,
                y - halfSize + size);
  view.DrawGraphicsRect(0, block, color);
}

void drawFilledRect(View &view, int32_t left, int32_t top, int32_t width,
                    int32_t height, ColorDefinition color) {
  GUIRect rect(left, top, left + width, top + height);
  view.DrawGraphicsRect(0, rect, color);
}

void drawRectOutline(View &view, int32_t left, int32_t top, int32_t width,
                     int32_t height, int32_t thickness, ColorDefinition color) {
  drawFilledRect(view, left, top, width, thickness, color);
  drawFilledRect(view, left, top + height - thickness, width, thickness, color);
  drawFilledRect(view, left, top, thickness, height, color);
  drawFilledRect(view, left + width - thickness, top, thickness, height, color);
}

void drawPanPixelLabel(View &view, int32_t centerX, int32_t top, PanLabel label,
                       ColorDefinition color) {
  constexpr int32_t glyphWidth = 3;
  constexpr int32_t glyphHeight = 5;
  constexpr int32_t pixelSize = 3;
  constexpr int32_t leftPattern[glyphHeight] = {
      0b100, 0b100, 0b100, 0b100, 0b111,
  };
  constexpr int32_t centerPattern[glyphHeight] = {
      0b111, 0b100, 0b100, 0b100, 0b111,
  };
  constexpr int32_t rightPattern[glyphHeight] = {
      0b110, 0b101, 0b110, 0b101, 0b101,
  };

  const int32_t *pattern = leftPattern;
  if (label == PanLabel::Center) {
    pattern = centerPattern;
  } else if (label == PanLabel::Right) {
    pattern = rightPattern;
  }

  const int32_t left = centerX - ((glyphWidth * pixelSize) / 2);
  for (int32_t row = 0; row < glyphHeight; ++row) {
    for (int32_t column = 0; column < glyphWidth; ++column) {
      const int32_t bit = 1 << (glyphWidth - column - 1);
      if ((pattern[row] & bit) == 0) {
        continue;
      }

      drawFilledRect(view, left + (column * pixelSize), top + (row * pixelSize),
                     pixelSize, pixelSize, color);
    }
  }
}

void drawInfPixelLabel(View &view, int32_t centerX, int32_t top,
                       ColorDefinition color) {
  constexpr int32_t glyphWidth = 3;
  constexpr int32_t glyphHeight = 5;
  constexpr int32_t pixelSize = 3;
  constexpr int32_t glyphGap = 1;
  constexpr int32_t patterns[3][glyphHeight] = {
      {0b111, 0b010, 0b010, 0b010, 0b111},
      {0b101, 0b111, 0b111, 0b111, 0b101},
      {0b111, 0b100, 0b110, 0b100, 0b100},
  };
  constexpr int32_t totalGlyphColumns = (glyphWidth * 3) + (glyphGap * 2);
  const int32_t left = centerX - ((totalGlyphColumns * pixelSize) / 2);

  for (int32_t glyph = 0; glyph < 3; ++glyph) {
    const int32_t glyphLeft =
        left + (glyph * (glyphWidth + glyphGap) * pixelSize);
    for (int32_t row = 0; row < glyphHeight; ++row) {
      for (int32_t column = 0; column < glyphWidth; ++column) {
        const int32_t bit = 1 << (glyphWidth - column - 1);
        if ((patterns[glyph][row] & bit) == 0) {
          continue;
        }

        drawFilledRect(view, glyphLeft + (column * pixelSize),
                       top + (row * pixelSize), pixelSize, pixelSize, color);
      }
    }
  }
}

void drawPixelDigit(View &view, int32_t centerX, int32_t top, int32_t digit,
                    int32_t pixelSize, ColorDefinition color) {
  constexpr int32_t glyphWidth = 3;
  constexpr int32_t glyphHeight = 5;
  constexpr int32_t onePattern[glyphHeight] = {
      0b010, 0b110, 0b010, 0b010, 0b111,
  };
  constexpr int32_t twoPattern[glyphHeight] = {
      0b111, 0b001, 0b111, 0b100, 0b111,
  };
  const int32_t *pattern = digit == 2 ? twoPattern : onePattern;
  const int32_t left = centerX - ((glyphWidth * pixelSize) / 2);

  for (int32_t row = 0; row < glyphHeight; ++row) {
    for (int32_t column = 0; column < glyphWidth; ++column) {
      const int32_t bit = 1 << (glyphWidth - column - 1);
      if ((pattern[row] & bit) == 0) {
        continue;
      }

      drawFilledRect(view, left + (column * pixelSize), top + (row * pixelSize),
                     pixelSize, pixelSize, color);
    }
  }
}

void drawChunkyLine(View &view, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                    ColorDefinition color,
                    int32_t chunkSize = EnvelopeLineChunkSize) {
  int32_t dx = x2 - x1;
  int32_t dy = y2 - y1;
  int32_t absDx = dx < 0 ? -dx : dx;
  int32_t absDy = dy < 0 ? -dy : dy;
  int32_t steps = std::max<int32_t>(absDx, absDy);

  if (steps == 0) {
    drawBlock(view, x1, y1, chunkSize, color);
    return;
  }

  for (int32_t step = 0; step <= steps; step += EnvelopeLineStep) {
    int32_t x = x1 + (dx * step) / steps;
    int32_t y = y1 + (dy * step) / steps;
    drawBlock(view, x, y, chunkSize, color);
  }
  drawBlock(view, x2, y2, chunkSize, color);
}

void drawEnvelopeBackground(View &view, int32_t x, int32_t y, int32_t width,
                            int32_t height) {
  GUIRect background(x, y, x + width, y + height);
  view.DrawGraphicsRect(0, background, CD_BACKGROUND);
}

void drawEnvelopeFrame(View &view, int32_t x, int32_t y, int32_t width,
                       int32_t height, ColorDefinition color) {
  GUIRect bottom(x, y + height - EnvelopeFrameThickness, x + width, y + height);
  GUIRect left(x, y, x + EnvelopeFrameThickness, y + height);
  GUIRect right(x + width - EnvelopeFrameThickness, y, x + width, y + height);
  GUIRect top(x, y, x + width, y + EnvelopeFrameThickness);
  view.DrawGraphicsRect(0, top, color);
  view.DrawGraphicsRect(0, bottom, color);
  view.DrawGraphicsRect(0, left, color);
  view.DrawGraphicsRect(0, right, color);
}

void drawEnvelopeGuideLine(View &view, int32_t x, int32_t top, int32_t bottom,
                           ColorDefinition color) {
  const int32_t clampedBottom = std::max(top, bottom);
  for (int32_t y = top; y <= clampedBottom; y += EnvelopeGuideLineStep) {
    const int32_t segmentBottom =
        std::min(clampedBottom + 1, y + EnvelopeGuideLineSegmentHeight);
    GUIRect segment(x, y, x + EnvelopeGuideLineThickness, segmentBottom);
    view.DrawGraphicsRect(0, segment, color);
  }
}

void drawEnvelopeAxis(View &view, int32_t x, int32_t y, int32_t width,
                      int32_t height, int32_t axisY) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const int32_t graphBottom = graphTop + std::max<int32_t>(graphHeight - 1, 0);
  const int32_t clampedAxisY =
      std::clamp<int32_t>(axisY, graphTop, graphBottom);
  drawChunkyLine(view, graphLeft, clampedAxisY, graphLeft + graphWidth,
                 clampedAxisY, CD_HILITE2, EnvelopeLineChunkSize - 2);
}

void drawEnvelopeShape(View &view, int32_t x, int32_t y, int32_t width,
                       int32_t height, const VoiceEnvelopeShapeValues &values,
                       int32_t signedDepth, ColorDefinition baseColor) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const VoiceEnvelopeGraphGeometry geometry =
      CalculateVoiceEnvelopeGraphGeometry(graphWidth, graphHeight, values,
                                          signedDepth);
  const int32_t startX = graphLeft;
  const int32_t startY = graphTop + geometry.axisY;
  const int32_t attackX = graphLeft + geometry.attackX;
  const int32_t peakY = graphTop + geometry.peakY;
  const int32_t decayX = graphLeft + geometry.decayX;
  const int32_t sustainX = graphLeft + geometry.sustainX;
  const int32_t sustainY = graphTop + geometry.sustainY;
  const int32_t releaseX = graphLeft + geometry.releaseX;
  const int32_t releaseY = graphTop + geometry.releaseY;
  drawEnvelopeAxis(view, x, y, width, height, startY);
  drawChunkyLine(view, startX, startY, attackX, peakY, baseColor);
  drawChunkyLine(view, attackX, peakY, decayX, sustainY, baseColor);
  drawChunkyLine(view, decayX, sustainY, sustainX, sustainY, baseColor);
  drawChunkyLine(view, sustainX, sustainY, releaseX, releaseY, baseColor);

  drawEnvelopeGuideLine(view, attackX, graphTop, peakY, baseColor);
  drawEnvelopeGuideLine(view, decayX, graphTop, sustainY, baseColor);
  drawEnvelopeGuideLine(view, sustainX, graphTop, sustainY, baseColor);
  if (releaseX != sustainX || geometry.releaseHold) {
    drawEnvelopeGuideLine(view, releaseX, graphTop, releaseY, baseColor);
  }

  drawBlock(view, startX, startY, EnvelopeNodeSize, baseColor);
  drawBlock(view, attackX, peakY, EnvelopeNodeSize, baseColor);
  drawBlock(view, decayX, sustainY, EnvelopeNodeSize, baseColor);
  drawBlock(view, sustainX, sustainY, EnvelopeNodeSize, baseColor);
  drawBlock(view, releaseX, releaseY, EnvelopeNodeSize, baseColor);
}

void drawBaseWidthShape(View &view, int32_t x, int32_t y, int32_t width,
                        int32_t height, int32_t base, int32_t widthValue,
                        ColorDefinition baseColor) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const int32_t graphRight = graphLeft + graphWidth;
  const int32_t lowY = graphTop + graphHeight - 1;
  const int32_t highY = graphTop + std::max<int32_t>(3, graphHeight / 4);
  const bool highpassActive = base > 0;
  const bool lowpassActive = widthValue < 0xFF;
  const int32_t rampInset = std::max<int32_t>(EnvelopeLineChunkSize * 2, 12);
  const int32_t baseX = graphLeft + (graphWidth * base) / 0xFF;
  const int32_t lowpassValue =
      lowpassActive
          ? (highpassActive ? base + ((widthValue * (0xFF - base)) / 0xFF)
                            : widthValue)
          : 0xFF;
  const int32_t lowpassX =
      graphLeft +
      (graphWidth * std::clamp<int32_t>(lowpassValue, 0, 0xFF)) / 0xFF;
  const int32_t riseStartX =
      highpassActive ? std::max(graphLeft, baseX - rampInset) : graphLeft;
  const int32_t fallEndX =
      lowpassActive ? std::min(graphRight, lowpassX + rampInset) : graphRight;
  const int32_t plateauStartX = highpassActive ? baseX : graphLeft;
  const int32_t plateauEndX =
      lowpassActive ? std::max(lowpassX, plateauStartX) : graphRight;
  const int32_t nodeSize = EnvelopeNodeSize - 2;

  if (highpassActive && riseStartX > graphLeft) {
    drawChunkyLine(view, graphLeft, lowY, riseStartX, lowY, baseColor);
  }
  drawChunkyLine(view, riseStartX, highpassActive ? lowY : highY, plateauStartX,
                 highY, baseColor);
  drawChunkyLine(view, plateauEndX, highY, fallEndX,
                 lowpassActive ? lowY : highY, baseColor);
  drawChunkyLine(view, plateauStartX, highY, plateauEndX, highY, baseColor);
  if (lowpassActive && fallEndX < graphRight) {
    drawChunkyLine(view, fallEndX, lowY, graphRight, lowY, baseColor);
  }

  drawEnvelopeGuideLine(view, plateauStartX, graphTop, highY, baseColor);
  if (plateauEndX != plateauStartX) {
    drawEnvelopeGuideLine(view, plateauEndX, graphTop, highY, baseColor);
  }

  drawBlock(view, plateauStartX, highY, nodeSize, baseColor);
  drawBlock(view, plateauEndX, highY, nodeSize, baseColor);
}

void drawTriggerLengthShape(View &view, int32_t x, int32_t y, int32_t width,
                            int32_t height, int32_t triggerLength,
                            ColorDefinition baseColor) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const int32_t graphRight = graphLeft + graphWidth;
  const int32_t lowY = graphTop + graphHeight - 1;
  const int32_t highY = graphTop + std::max<int32_t>(3, graphHeight / 4);
  const int32_t length = std::clamp<int32_t>(triggerLength, 0, 96);
  const bool infinite = length == 0;
  const int32_t gateWidth = infinite ? graphWidth : (graphWidth * length) / 96;
  const int32_t gateEndX = std::min<int32_t>(
      graphRight, graphLeft + std::max<int32_t>(gateWidth, 1));

  drawChunkyLine(view, graphLeft, lowY, graphLeft, highY, baseColor);
  drawChunkyLine(view, graphLeft, highY, gateEndX, highY, baseColor);
  if (!infinite) {
    drawChunkyLine(view, gateEndX, highY, gateEndX, lowY, baseColor);
  }
  if (!infinite && gateEndX < graphRight) {
    drawChunkyLine(view, gateEndX, lowY, graphRight, lowY, baseColor);
  }

  if (infinite) {
    drawInfPixelLabel(view, graphLeft + (graphWidth / 2), graphTop + 2,
                      baseColor);
  } else {
    drawEnvelopeGuideLine(view, gateEndX, graphTop, highY, baseColor);
  }
  drawBlock(view, graphLeft, highY, EnvelopeNodeSize - 2, baseColor);
  drawBlock(view, gateEndX, highY, EnvelopeNodeSize - 2, baseColor);
}

void drawPanShape(View &view, int32_t x, int32_t y, int32_t width,
                  int32_t height, int32_t pan, ColorDefinition baseColor) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const int32_t graphRight = graphLeft + graphWidth;
  const int32_t centerY = graphTop + (graphHeight / 2);
  const int32_t panValue = std::clamp<int32_t>(pan, 0, 0xFF);
  const int32_t markerX = graphLeft + (graphWidth * panValue) / 0xFF;
  const bool centered = panValue == 0x7F;
  const int32_t markerTop =
      graphTop + (centered ? 2 : std::max<int32_t>(4, graphHeight / 5));
  const int32_t markerBottom =
      graphTop + graphHeight -
      (centered ? 2 : std::max<int32_t>(4, graphHeight / 5));

  drawPanPixelLabel(view, graphLeft + 10, graphTop + 2, PanLabel::Left,
                    baseColor);
  drawPanPixelLabel(view, graphLeft + (graphWidth / 2) + 10, graphTop + 2,
                    PanLabel::Center, baseColor);
  drawPanPixelLabel(view, graphRight - 10, graphTop + 2, PanLabel::Right,
                    baseColor);
  drawChunkyLine(view, graphLeft, centerY, graphRight, centerY, baseColor,
                 EnvelopeLineChunkSize - 1);
  drawEnvelopeGuideLine(view, graphLeft + (graphWidth / 2), graphTop,
                        graphTop + graphHeight - 1, baseColor);
  drawChunkyLine(view, markerX, markerTop, markerX, markerBottom, baseColor,
                 centered ? EnvelopeLineChunkSize + 2 : EnvelopeLineChunkSize);
  drawBlock(view, markerX, centerY,
            centered ? EnvelopeNodeSize : EnvelopeNodeSize - 2, baseColor);
}

void drawSelectionTicks(View &view, int32_t left, int32_t top, int32_t width,
                        int32_t height, ColorDefinition color) {
  constexpr int32_t tickLength = 8;
  constexpr int32_t tickThickness = 3;
  drawFilledRect(view, left - 4, top - 4, tickLength, tickThickness, color);
  drawFilledRect(view, left - 4, top - 4, tickThickness, tickLength, color);
  drawFilledRect(view, left + width - tickLength + 4, top - 4, tickLength,
                 tickThickness, color);
  drawFilledRect(view, left + width + 1, top - 4, tickThickness, tickLength,
                 color);
  drawFilledRect(view, left - 4, top + height + 1, tickLength, tickThickness,
                 color);
  drawFilledRect(view, left - 4, top + height - tickLength + 4, tickThickness,
                 tickLength, color);
  drawFilledRect(view, left + width - tickLength + 4, top + height + 1,
                 tickLength, tickThickness, color);
  drawFilledRect(view, left + width + 1, top + height - tickLength + 4,
                 tickThickness, tickLength, color);
}

void drawInsertModule(View &view, int32_t centerX, int32_t centerY,
                      int32_t slotIndex, const InsertGraphSlot &slot,
                      bool selected) {
  constexpr int32_t amountHeight = 6;
  constexpr int32_t amountGap = 5;
  const bool active =
      slot.type != kVoiceInsertEffectPassthrough && slot.amount > 0;
  const ColorDefinition moduleColor =
      selected ? CD_HILITE2 : (active ? CD_EMPHASIS : CD_NORMAL);
  const int32_t left = centerX - (InsertModuleWidth / 2);
  const int32_t top = centerY - (InsertModuleHeight / 2);
  const int32_t borderThickness = selected ? 3 : 2;
  const int32_t amountTop = top + InsertModuleHeight + amountGap;
  const int32_t amountWidth =
      std::max<int32_t>(2, ((InsertModuleWidth - 8) * slot.amount) / 0xFF);

  drawRectOutline(view, left, top, InsertModuleWidth, InsertModuleHeight,
                  borderThickness, moduleColor);
  drawPixelDigit(view, centerX, top + 6, slotIndex + 1, 3, moduleColor);
  drawRectOutline(view, left + 4, amountTop, InsertModuleWidth - 8,
                  amountHeight, 1, moduleColor);

  if (slot.amount > 0) {
    drawFilledRect(view, left + 5, amountTop + 1, amountWidth, amountHeight - 2,
                   moduleColor);
  }
  if (selected) {
    drawSelectionTicks(view, left, top, InsertModuleWidth, InsertModuleHeight,
                       moduleColor);
  }
}

void drawRailWithModuleGaps(View &view, int32_t startX, int32_t endX, int32_t y,
                            const InsertBlockPosition *preBlocks,
                            int32_t preCount,
                            const InsertBlockPosition *postBlocks,
                            int32_t postCount, ColorDefinition color) {
  int32_t gapLeft[2];
  int32_t gapRight[2];
  int32_t gapCount = 0;
  for (int32_t i = 0; i < preCount; ++i) {
    gapLeft[gapCount] = preBlocks[i].x - (InsertModuleWidth / 2);
    gapRight[gapCount] = preBlocks[i].x + (InsertModuleWidth / 2);
    ++gapCount;
  }
  for (int32_t i = 0; i < postCount; ++i) {
    gapLeft[gapCount] = postBlocks[i].x - (InsertModuleWidth / 2);
    gapRight[gapCount] = postBlocks[i].x + (InsertModuleWidth / 2);
    ++gapCount;
  }

  for (int32_t i = 0; i < gapCount; ++i) {
    for (int32_t j = i + 1; j < gapCount; ++j) {
      if (gapLeft[j] < gapLeft[i]) {
        std::swap(gapLeft[i], gapLeft[j]);
        std::swap(gapRight[i], gapRight[j]);
      }
    }
  }

  int32_t cursorX = startX;
  for (int32_t i = 0; i < gapCount; ++i) {
    const int32_t segmentEnd = std::clamp<int32_t>(gapLeft[i], startX, endX);
    if (segmentEnd > cursorX) {
      drawChunkyLine(view, cursorX, y, segmentEnd, y, color,
                     EnvelopeLineChunkSize - 1);
    }
    cursorX = std::max(cursorX, std::clamp<int32_t>(gapRight[i], startX, endX));
  }

  if (cursorX < endX) {
    drawChunkyLine(view, cursorX, y, endX, y, color, EnvelopeLineChunkSize - 1);
  }
}

void drawInsertCore(View &view, int32_t centerX, int32_t centerY,
                    ColorDefinition color) {
  constexpr int32_t coreSize = 10;
  constexpr int32_t coreGap = 8;
  drawBlock(view, centerX - coreSize - coreGap, centerY, coreSize, color);
  drawBlock(view, centerX, centerY, coreSize + 2, color);
  drawBlock(view, centerX + coreSize + coreGap, centerY, coreSize, color);
}

void drawInsertFlowShape(View &view, int32_t x, int32_t y, int32_t width,
                         int32_t height, const InsertGraphSlot (&slots)[2],
                         VoiceView::GraphSection selectedSection,
                         ColorDefinition baseColor) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const int32_t graphRight = graphLeft + graphWidth;
  const int32_t centerY = graphTop + (graphHeight / 2);
  const int32_t srcX = graphLeft + 22;
  const int32_t coreX = graphLeft + (graphWidth / 2);
  const int32_t outX = graphRight - 22;
  const int32_t preStartX = graphLeft + 80;
  const int32_t preEndX = coreX - 60;
  const int32_t postStartX = coreX + 60;
  const int32_t postEndX = graphRight - 80;

  InsertBlockPosition preBlocks[2];
  InsertBlockPosition postBlocks[2];
  int32_t preCount = 0;
  int32_t postCount = 0;
  for (int32_t slot = 0; slot < 2; ++slot) {
    if (slots[slot].position == kVoiceInsertPositionPost) {
      postBlocks[postCount] = {.slot = slot, .x = 0};
      ++postCount;
    } else {
      preBlocks[preCount] = {.slot = slot, .x = 0};
      ++preCount;
    }
  }

  for (int32_t i = 0; i < preCount; ++i) {
    preBlocks[i].x = preCount == 1 ? (preStartX + preEndX) / 2
                                   : preStartX + ((preEndX - preStartX) * i);
  }
  for (int32_t i = 0; i < postCount; ++i) {
    postBlocks[i].x = postCount == 1
                          ? (postStartX + postEndX) / 2
                          : postStartX + ((postEndX - postStartX) * i);
  }

  drawRailWithModuleGaps(view, srcX, outX, centerY, preBlocks, preCount,
                         postBlocks, postCount, baseColor);
  drawBlock(view, srcX, centerY, EnvelopeNodeSize - 2, baseColor);
  drawBlock(view, outX, centerY, EnvelopeNodeSize - 2, baseColor);
  drawInsertCore(view, coreX, centerY, baseColor);

  const auto isSlotSelected = [selectedSection](int32_t slot) {
    return selectedSection == (slot == 0 ? VoiceView::GraphSection::FxType1
                                         : VoiceView::GraphSection::FxType2) ||
           selectedSection == (slot == 0
                                   ? VoiceView::GraphSection::FxAmount1
                                   : VoiceView::GraphSection::FxAmount2) ||
           selectedSection == (slot == 0
                                   ? VoiceView::GraphSection::FxPosition1
                                   : VoiceView::GraphSection::FxPosition2);
  };

  for (int32_t i = 0; i < preCount; ++i) {
    const int32_t slot = preBlocks[i].slot;
    drawInsertModule(view, preBlocks[i].x, centerY, slot, slots[slot],
                     isSlotSelected(slot));
  }
  for (int32_t i = 0; i < postCount; ++i) {
    const int32_t slot = postBlocks[i].slot;
    drawInsertModule(view, postBlocks[i].x, centerY, slot, slots[slot],
                     isSlotSelected(slot));
  }
}

void drawFilterResponseShape(View &view, int32_t x, int32_t y, int32_t width,
                             int32_t height, int32_t cutoff, int32_t resonance,
                             int32_t filterType, ColorDefinition baseColor) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const VoiceFilterGraphGeometry geometry = CalculateVoiceFilterGraphGeometry(
      graphWidth, graphHeight, cutoff, resonance, filterType);
  const int32_t leftX = graphLeft;
  const int32_t leftShoulderX = graphLeft + geometry.leftShoulderX;
  const int32_t peakX = graphLeft + geometry.cutoffX;
  const int32_t rightShoulderX = graphLeft + geometry.rightShoulderX;
  const int32_t rightX = graphLeft + graphWidth;
  const int32_t leftY = graphTop + geometry.leftY;
  const int32_t peakY = graphTop + geometry.peakY;
  const int32_t rightY = graphTop + geometry.rightY;
  const int32_t nodeSize = EnvelopeNodeSize - 2;

  drawChunkyLine(view, leftX, leftY, leftShoulderX, leftY, baseColor);
  drawChunkyLine(view, leftShoulderX, leftY, peakX, peakY, baseColor);
  drawChunkyLine(view, peakX, peakY, rightShoulderX, rightY, baseColor);
  drawChunkyLine(view, rightShoulderX, rightY, rightX, rightY, baseColor);

  drawEnvelopeGuideLine(view, peakX, graphTop, peakY, baseColor);
  drawBlock(view, leftX, leftY, nodeSize, baseColor);
  drawBlock(view, leftShoulderX, leftY, nodeSize, baseColor);
  drawBlock(view, peakX, peakY, EnvelopeNodeSize, baseColor);
  drawBlock(view, rightShoulderX, rightY, nodeSize, baseColor);
  drawBlock(view, rightX, rightY, nodeSize, baseColor);
}

float lfoShapeValue(LFOShape shape, float phase, uint32_t randomStep) {
  const float wrappedPhase = phase - std::floor(phase);
  switch (shape) {
  case LFOShape::Sine:
    return std::sin(2.0f * LFOGraphPi * wrappedPhase);

  case LFOShape::Triangle:
    if (wrappedPhase < 0.25f) {
      return wrappedPhase * 4.0f;
    }
    if (wrappedPhase < 0.75f) {
      return 2.0f - (wrappedPhase * 4.0f);
    }
    return (wrappedPhase * 4.0f) - 4.0f;

  case LFOShape::Square:
    return wrappedPhase < 0.5f ? 1.0f : -1.0f;

  case LFOShape::Ramp:
    return (wrappedPhase * 2.0f) - 1.0f;

  case LFOShape::Random: {
    uint32_t value = randomStep * 1103515245u + 12345u;
    value ^= value >> 16;
    return (static_cast<float>(value & 0xFFFFu) / 32767.5f) - 1.0f;
  }

  case LFOShape::Last:
    break;
  }

  return 0.0f;
}

float lfoDisplayCycles(const LFOConfig &config) {
  if (config.rateMode_ == LFORateMode::Sync) {
    return std::clamp(GetLFOSyncDivision(static_cast<size_t>(config.syncIndex_))
                          .cyclesPerBeat_,
                      0.5f, 8.0f);
  }

  const float normalized =
      static_cast<float>(std::clamp(config.rate_, LFOSettings::MinRateValue,
                                    LFOSettings::MaxRateValue)) /
      static_cast<float>(LFOSettings::MaxRateValue);
  return 0.75f + (normalized * 5.25f);
}

bool getLFOTargetFieldPosition(LFOTarget target, const GUIPoint &anchor,
                               GUIPoint &position) {
  const int32_t topBaseY = anchor._y + 3;
  const int32_t bottomBaseY = topBaseY + 9;
  const int32_t fxBaseY = bottomBaseY - 3;

  switch (target) {
  case LFOTarget::VoiceBandpassCutoff:
    position = GUIPoint(VoiceColumnX, topBaseY + 3);
    return true;
  case LFOTarget::VoiceBandpassMix:
    position = GUIPoint(VoiceColumnX, topBaseY + 4);
    return true;
  case LFOTarget::VoiceFilterCutoff:
    position = GUIPoint(FilterColumnX, topBaseY + 2);
    return true;
  case LFOTarget::VoiceFilterResonance:
    position = GUIPoint(FilterColumnX, topBaseY + 3);
    return true;
  case LFOTarget::VoiceAmpAttack:
    position = GUIPoint(AmpColumnX, topBaseY + 2);
    return true;
  case LFOTarget::VoiceAmpDecay:
    position = GUIPoint(AmpColumnX, topBaseY + 3);
    return true;
  case LFOTarget::VoiceAmpSustain:
    position = GUIPoint(AmpColumnX, topBaseY + 4);
    return true;
  case LFOTarget::VoiceAmpRelease:
    position = GUIPoint(AmpColumnX, topBaseY + 5);
    return true;
  case LFOTarget::VoiceFilterAttack:
    position = GUIPoint(FilterColumnX, topBaseY + 5);
    return true;
  case LFOTarget::VoiceFilterDecay:
    position = GUIPoint(FilterColumnX, topBaseY + 6);
    return true;
  case LFOTarget::VoiceFilterSustain:
    position = GUIPoint(FilterColumnX, topBaseY + 7);
    return true;
  case LFOTarget::VoiceFilterRelease:
    position = GUIPoint(FilterColumnX, topBaseY + 8);
    return true;
  case LFOTarget::VoiceFilterEnvAmount:
    position = GUIPoint(FilterColumnX, topBaseY + 4);
    return true;
  case LFOTarget::VoiceAmp:
    position = GUIPoint(AmpColumnX, topBaseY + 1);
    return true;
  case LFOTarget::VoicePan:
    position = GUIPoint(VoiceColumnX, topBaseY + 1);
    return true;
  case LFOTarget::VoiceInsertAmount1:
    position = GUIPoint(VoiceColumnX, fxBaseY + 2);
    return true;
  case LFOTarget::VoiceInsertAmount2:
    position = GUIPoint(VoiceColumnX, fxBaseY + 5);
    return true;
  case LFOTarget::None:
  case LFOTarget::Last:
    break;
  }

  return false;
}

void drawLFOShape(View &view, int32_t x, int32_t y, int32_t width,
                  int32_t height, const LFOConfig &config,
                  ColorDefinition baseColor) {
  const int32_t graphLeft = x + EnvelopePaddingX;
  const int32_t graphTop = y + EnvelopePaddingTop;
  const int32_t graphWidth = width - (EnvelopePaddingX * 2);
  const int32_t graphHeight =
      height - EnvelopePaddingTop - EnvelopePaddingBottom;
  const int32_t graphRight = graphLeft + graphWidth;
  const int32_t centerY = graphTop + (graphHeight / 2);
  const float cycles = lfoDisplayCycles(config);
  int minValue = 0;
  int maxValue = 0;
  int32_t depthMax = 0xFF;

  if (GetLFOTargetValueRange(config.target_, minValue, maxValue)) {
    depthMax = std::max<int32_t>(static_cast<int32_t>(std::abs(minValue)),
                                 static_cast<int32_t>(std::abs(maxValue)));
    if (depthMax <= 0) {
      depthMax = 0xFF;
    }
  }

  const int32_t depth =
      std::clamp<int32_t>(config.depth_, 0, std::max<int32_t>(1, depthMax));
  const float depthScale =
      std::sqrt(static_cast<float>(depth) / static_cast<float>(depthMax));
  const int32_t amplitude = static_cast<int32_t>(
      static_cast<float>(std::max<int32_t>(2, (graphHeight / 2) - 2)) *
      depthScale);
  drawChunkyLine(view, graphLeft, centerY, graphRight, centerY, baseColor,
                 EnvelopeLineChunkSize - 2);

  int32_t previousX = graphLeft;
  int32_t previousY = centerY;
  for (int32_t sample = 0; sample <= graphWidth; sample += EnvelopeLineStep) {
    const float samplePosition =
        static_cast<float>(sample) / static_cast<float>(graphWidth);
    const float phase = samplePosition * cycles;
    const int32_t xPos = graphLeft + sample;
    const int32_t yPos =
        centerY - static_cast<int32_t>(
                      lfoShapeValue(config.shape_, phase,
                                    static_cast<uint32_t>(phase * 4.0f)) *
                      static_cast<float>(amplitude));

    if (sample > 0) {
      drawChunkyLine(view, previousX, previousY, xPos, yPos, baseColor);
    }
    previousX = xPos;
    previousY = yPos;
  }
}
} // namespace

GraphicsLayout VoiceView::GetGraphicsLayout() {
  static constexpr GraphicsRegion regions[] = {{EnvelopePanelX, EnvelopeGraphY,
                                                EnvelopePanelWidth,
                                                EnvelopeGraphHeight}};
  return {regions, 1};
}

VoiceView::VoiceView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), lastFocusIndex_(0), graphKind_(GraphKind::Voice),
      graphSection_(GraphSection::None), previewMode_(PreviewMode::None),
      previewChannel_(0), previewInstrument_(0) {}

VoiceView::~VoiceView() {}

void VoiceView::Reset() {
  lastFocusIndex_ = 0;
  graphKind_ = GraphKind::Voice;
  graphSection_ = GraphSection::None;
  previewMode_ = PreviewMode::None;
}

I_Instrument *VoiceView::getInstrument() {
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  return bank->GetInstrument(viewData_->currentInstrumentID_);
}

uint8_t VoiceView::voicePreviewNote(I_Instrument *instrument) {
  if (!instrument || instrument->GetType() != IT_SAMPLE_SOURCE) {
    return NOTE_C3;
  }
  return static_cast<SampleSourceInstrument *>(instrument)->GetPreviewNote();
}

bool VoiceView::startVoicePreview(unsigned short mask) {
  if (!IsInstrumentPreviewButtonMask(mask, EPBM_PLAY, EPBM_ALT)) {
    return false;
  }

  I_Instrument *instrument = getInstrument();
  if (!SupportsInstrumentPreview(instrument) || !viewData_->song_) {
    return true;
  }

  if (previewMode_ != PreviewMode::None) {
    return true;
  }

  int channelCount = SONG_CHANNEL_COUNT;
  if (channelCount <= 0) {
    return true;
  }

  previewInstrument_ =
      static_cast<unsigned short>(viewData_->currentInstrumentID_);
  previewChannel_ = static_cast<unsigned short>(channelCount - 1);
  Player::GetInstance()->PlayNote(previewInstrument_, previewChannel_,
                                  voicePreviewNote(instrument), 0x7F);
  previewMode_ = PreviewMode::Manual;
  return true;
}

void VoiceView::auditionParameterChange() {
  Config *config = Config::GetInstance();
  I_Instrument *instrument = getInstrument();
  Player *player = Player::GetInstance();
  if (!config || !config->IsInstrumentAuditionEnabled() || !instrument ||
      !viewData_->song_ || !player || player->IsRunning()) {
    return;
  }

  if (previewMode_ != PreviewMode::None) {
    stopVoicePreview();
  }
  previewInstrument_ =
      static_cast<unsigned short>(viewData_->currentInstrumentID_);
  previewChannel_ = static_cast<unsigned short>(SONG_CHANNEL_COUNT - 1);
  player->PlayNote(previewInstrument_, previewChannel_,
                   voicePreviewNote(instrument), 0x7F);
  previewMode_ = PreviewMode::Parameter;
}

void VoiceView::stopVoicePreview() {
  if (previewMode_ == PreviewMode::None) {
    return;
  }

  Player::GetInstance()->StopNote(previewInstrument_, previewChannel_);
  previewMode_ = PreviewMode::None;
}

void VoiceView::warpToNext(int offset) {
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  if (!bank) {
    return;
  }

  int currentID = viewData_->currentInstrumentID_;
  int count = MAX_INSTRUMENT_COUNT;

  while (count > 0) {
    int nextID = currentID + offset;
    if (nextID >= MAX_INSTRUMENT_COUNT) {
      nextID = nextID - MAX_INSTRUMENT_COUNT;
    } else if (nextID < 0) {
      nextID = MAX_INSTRUMENT_COUNT + nextID;
    }

    I_Instrument *instr = bank->GetInstrument(nextID);
    if (SupportsAudioVoiceScreen(instr)) {
      viewData_->currentInstrumentID_ = nextID;
      refreshVoiceFields();
      isDirty_ = true;
      return;
    }

    currentID = nextID;
    count--;
  }
}

void VoiceView::refreshVoiceFields() {
  ClearFocus();
  fieldList_.clear();
  intVarField_.clear();
  staticField_.clear();
  lfoFields_.clear();

  I_Instrument *instrument = getInstrument();
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  int instrumentID = viewData_->currentInstrumentID_;
  VoiceParameters *voiceParams = SupportsAudioVoiceScreen(instrument)
                                     ? bank->GetVoiceParameters(instrumentID)
                                     : nullptr;
  const GUIPoint anchor = GetAnchor();

  if (!instrument) {
    staticField_.emplace_back(anchor, "Voice unavailable");
    fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));
    isDirty_ = true;
    return;
  }

  const int topBaseY = anchor._y + 3;
  const int bottomBaseY = topBaseY + 9;
  const int fxBaseY = bottomBaseY - 3;
  const int lfoBaseY = bottomBaseY + 1;

  const auto addStatic = [this](int x, int y, const char *label) {
    staticField_.emplace_back(GUIPoint(x, y), label);
    fieldList_.insert(fieldList_.end(), &staticField_.back());
  };

  const auto addIntField = [this, bank, instrument, instrumentID](
                               int x, int y, FourCC id, const char *format,
                               int min, int max, int xOffset = 1,
                               int yOffset = 0x10, int displayOffset = 0) {
    Variable *var = findVoiceVariable(bank, instrument, instrumentID, id);
    if (!var) {
      return;
    }

    intVarField_.emplace_back(GUIPoint(x, y), *var, format, min, max, xOffset,
                              yOffset, displayOffset);
    fieldList_.insert(fieldList_.end(), &intVarField_.back());
  };

  const auto addLFOField = [this, voiceParams, addStatic](int x, int y,
                                                          size_t lfoIndex,
                                                          LFOFieldKind kind) {
    if (!voiceParams) {
      addStatic(x, y, "Lfo N/A");
      return;
    }

    lfoFields_.emplace_back(GUIPoint(x, y), *voiceParams, lfoIndex, kind);
    // The last three LFO rows sit beside the map, in the footer grid.
    if (kind == LFOFieldKind::RateMode || kind == LFOFieldKind::Rate ||
        kind == LFOFieldKind::Depth)
      lfoFields_.back().SetTextRegion(TEXT_REGION_BODY_LOWER, 4, 19);
    fieldList_.insert(fieldList_.end(), &lfoFields_.back());
  };

  const auto addInsertType = [this, bank, instrument,
                              instrumentID](int x, int y, int slot) {
    Variable *typeVar = findVoiceVariable(bank, instrument, instrumentID,
                                          GetVoiceInsertTypeFourCC(slot));
    if (typeVar) {
      const char *format = slot == 0 ? "FX1: %s" : "FX2: %s";
      intVarField_.emplace_back(GUIPoint(x, y), *typeVar, format, 0,
                                kVoiceInsertEffectCount - 1, 1, 1, 0);
      fieldList_.insert(fieldList_.end(), &intVarField_.back());
    }
  };

  addStatic(VoiceColumnX, topBaseY, "Voice");
  addStatic(FilterColumnX, topBaseY, "Filter");
  addStatic(AmpColumnX, topBaseY, "Amp");

  addIntField(VoiceColumnX, topBaseY + 2, FourCC::VoiceTriggerLength,
              "Len:  %d", 0, 96);
  addIntField(FilterColumnX, topBaseY + 1, FourCC::VoiceFilterType, "Typ: %02X",
              0, 0xFF);
  addIntField(AmpColumnX, topBaseY + 1, FourCC::VoiceAmp, "Lvl: %02X", 0, 0xFF);

  addIntField(FilterColumnX, topBaseY + 2, FourCC::VoiceFilterCutoff,
              "Cut: %02X", 0, 0xFF);
  addIntField(AmpColumnX, topBaseY + 2, FourCC::VoiceAmpAttack, "Atk: %02X", 0,
              0xFF);

  addIntField(VoiceColumnX, topBaseY + 3, FourCC::VoiceBandpassCutoff,
              "BPB: %X", 0, 0xFF);
  addIntField(FilterColumnX, topBaseY + 3, FourCC::VoiceFilterResonance,
              "Res: %02X", 0, 0xFF);
  addIntField(AmpColumnX, topBaseY + 3, FourCC::VoiceAmpDecay, "Dec: %02X", 0,
              0xFF);

  addIntField(VoiceColumnX, topBaseY + 4, FourCC::VoiceBandpassMix, "BPW: %X",
              0, 0xFF);
  addIntField(FilterColumnX, topBaseY + 4, FourCC::VoiceFilterEnvAmount,
              "Env: %02d", -64, 64);
  addIntField(AmpColumnX, topBaseY + 4, FourCC::VoiceAmpSustain, "Sus: %02X", 0,
              0xFF);

  addIntField(VoiceColumnX, topBaseY + 1, FourCC::VoicePan, "Pan: %02X", 0,
              0xFF);

  addIntField(FilterColumnX, topBaseY + 5, FourCC::VoiceFilterAttack,
              "Atk: %02X", 0, 0xFF);
  addIntField(AmpColumnX, topBaseY + 5, FourCC::VoiceAmpRelease, "Rel: %02X", 0,
              0xFF);

  addIntField(FilterColumnX, topBaseY + 6, FourCC::VoiceFilterDecay,
              "Dec: %02X", 0, 0xFF);

  addIntField(FilterColumnX, topBaseY + 7, FourCC::VoiceFilterSustain,
              "Sus: %02X", 0, 0xFF);

  addIntField(FilterColumnX, topBaseY + 8, FourCC::VoiceFilterRelease,
              "Rel: %02X", 0, 0xFF);

  addStatic(VoiceColumnX, fxBaseY, "Fx");
  addStatic(FilterColumnX, lfoBaseY, "LFO1");
  addStatic(AmpColumnX, lfoBaseY, "LFO2");

  addInsertType(VoiceColumnX, fxBaseY + 1, 0);
  addLFOField(FilterColumnX, lfoBaseY + 1, 0, LFOFieldKind::Target);
  addLFOField(AmpColumnX, lfoBaseY + 1, 1, LFOFieldKind::Target);

  addIntField(VoiceColumnX, fxBaseY + 2, GetVoiceInsertAmountFourCC(0),
              "Amt: %02X", 0, 0xFF, 1, 0x10);
  addLFOField(FilterColumnX, lfoBaseY + 2, 0, LFOFieldKind::Shape);
  addLFOField(AmpColumnX, lfoBaseY + 2, 1, LFOFieldKind::Shape);

  addIntField(VoiceColumnX, fxBaseY + 3, GetVoiceInsertPositionFourCC(0),
              "Pos: %s", 0, kVoiceInsertPositionCount - 1, 1, 1);

  addInsertType(VoiceColumnX, fxBaseY + 4, 1);
  addLFOField(FilterColumnX, lfoBaseY + 3, 0, LFOFieldKind::Trigger);
  addLFOField(AmpColumnX, lfoBaseY + 3, 1, LFOFieldKind::Trigger);

  addIntField(VoiceColumnX, fxBaseY + 5, GetVoiceInsertAmountFourCC(1),
              "Amt: %02X", 0, 0xFF, 1, 0x10);
  addLFOField(FilterColumnX, lfoBaseY + 4, 0, LFOFieldKind::RateMode);
  addLFOField(AmpColumnX, lfoBaseY + 4, 1, LFOFieldKind::RateMode);

  addIntField(VoiceColumnX, fxBaseY + 6, GetVoiceInsertPositionFourCC(1),
              "Pos: %s", 0, kVoiceInsertPositionCount - 1, 1, 1);

  addLFOField(FilterColumnX, lfoBaseY + 5, 0, LFOFieldKind::Rate);
  addLFOField(AmpColumnX, lfoBaseY + 5, 1, LFOFieldKind::Rate);

  addLFOField(FilterColumnX, lfoBaseY + 6, 0, LFOFieldKind::Depth);
  addLFOField(AmpColumnX, lfoBaseY + 6, 1, LFOFieldKind::Depth);

  if (lastFocusIndex_ >= 0 &&
      lastFocusIndex_ < static_cast<int>(fieldList_.size())) {
    int index = 0;
    for (auto *field : fieldList_) {
      if (index == lastFocusIndex_ && !field->IsStatic()) {
        SetFocus(field);
        break;
      }
      index++;
    }
  }
  if (!GetFocus()) {
    for (auto *field : fieldList_) {
      if (!field->IsStatic()) {
        SetFocus(field);
        break;
      }
    }
  }
  updateGraphSelectionFromFocus();
  isDirty_ = true;
}

VoiceView::GraphKind VoiceView::graphKindForField(UIField *field) {
  if (!field || field->IsStatic()) {
    return graphKind_;
  }

  const GUIPoint anchor = GetAnchor();
  const int32_t topBaseY = anchor._y + 3;
  const int32_t bottomBaseY = topBaseY + 9;
  const int32_t fxBaseY = bottomBaseY - 3;
  const int32_t lfoBaseY = bottomBaseY + 1;
  const int32_t x = field->GetPosition()._x;
  const int32_t y = field->GetPosition()._y;

  if (x == VoiceColumnX && y >= fxBaseY + 1 && y <= fxBaseY + 6) {
    return GraphKind::Fx;
  }
  if (x == FilterColumnX && y >= lfoBaseY + 1 && y <= lfoBaseY + 6) {
    return GraphKind::LFO1;
  }
  if (x == AmpColumnX && y >= lfoBaseY + 1 && y <= lfoBaseY + 6) {
    return GraphKind::LFO2;
  }
  if (x == AmpColumnX && y >= topBaseY + 1 && y <= topBaseY + 5) {
    return GraphKind::Amp;
  }
  if (x == FilterColumnX && y >= topBaseY + 1 && y <= topBaseY + 8) {
    return GraphKind::Filter;
  }
  if (x == VoiceColumnX && y >= topBaseY + 1 && y <= topBaseY + 4) {
    return GraphKind::Voice;
  }

  return graphKind_;
}

VoiceView::GraphSection VoiceView::graphSectionForField(UIField *field) {
  if (!field || field->IsStatic()) {
    return graphSection_;
  }

  const GUIPoint anchor = GetAnchor();
  const int32_t topBaseY = anchor._y + 3;
  const int32_t bottomBaseY = topBaseY + 9;
  const int32_t fxBaseY = bottomBaseY - 3;
  const int32_t lfoBaseY = bottomBaseY + 1;
  const int32_t x = field->GetPosition()._x;
  const int32_t y = field->GetPosition()._y;

  if ((x == FilterColumnX || x == AmpColumnX) && y >= lfoBaseY + 1 &&
      y <= lfoBaseY + 6) {
    if (y == lfoBaseY + 2) {
      return GraphSection::LFOShape;
    }
    if (y == lfoBaseY + 5) {
      return GraphSection::LFORate;
    }
    if (y == lfoBaseY + 6) {
      return GraphSection::LFODepth;
    }
    return GraphSection::None;
  }

  if (x == VoiceColumnX) {
    if (y == topBaseY + 1) {
      return GraphSection::Pan;
    }
    if (y == topBaseY + 2) {
      return GraphSection::TriggerLength;
    }
    if (y == topBaseY + 3) {
      return GraphSection::VoiceBase;
    }
    if (y == topBaseY + 4) {
      return GraphSection::VoiceWidth;
    }
    if (y == fxBaseY + 1) {
      return GraphSection::FxType1;
    }
    if (y == fxBaseY + 2) {
      return GraphSection::FxAmount1;
    }
    if (y == fxBaseY + 3) {
      return GraphSection::FxPosition1;
    }
    if (y == fxBaseY + 4) {
      return GraphSection::FxType2;
    }
    if (y == fxBaseY + 5) {
      return GraphSection::FxAmount2;
    }
    if (y == fxBaseY + 6) {
      return GraphSection::FxPosition2;
    }
    return GraphSection::None;
  }

  if (x == FilterColumnX) {
    if (y == topBaseY + 1) {
      return GraphSection::FilterType;
    }
    if (y == topBaseY + 2) {
      return GraphSection::FilterCutoff;
    }
    if (y == topBaseY + 3) {
      return GraphSection::FilterResonance;
    }
    if (y == topBaseY + 4) {
      return GraphSection::All;
    }
    if (y == topBaseY + 5) {
      return GraphSection::Attack;
    }
    if (y == topBaseY + 6) {
      return GraphSection::Decay;
    }
    if (y == topBaseY + 7) {
      return GraphSection::Sustain;
    }
    if (y == topBaseY + 8) {
      return GraphSection::Release;
    }
    return GraphSection::None;
  }

  if (x == AmpColumnX) {
    if (y == topBaseY + 1) {
      return GraphSection::All;
    }
    if (y == topBaseY + 2) {
      return GraphSection::Attack;
    }
    if (y == topBaseY + 3) {
      return GraphSection::Decay;
    }
    if (y == topBaseY + 4) {
      return GraphSection::Sustain;
    }
    if (y == topBaseY + 5) {
      return GraphSection::Release;
    }
    return GraphSection::None;
  }

  return graphSection_;
}

void VoiceView::updateGraphSelectionFromFocus() {
  UIField *focus = GetFocus();
  if (!focus || focus->IsStatic()) {
    return;
  }

  const GraphKind nextGraphKind = graphKindForField(focus);
  const GraphSection nextGraphSection = graphSectionForField(focus);
  if (graphKind_ != nextGraphKind || graphSection_ != nextGraphSection) {
    graphKind_ = nextGraphKind;
    graphSection_ = nextGraphSection;
    isDirty_ = true;
  }
}

bool VoiceView::moveFocus(int32_t xDirection, int32_t yDirection) {
  UIField *focus = GetFocus();
  if (!focus) {
    for (auto *field : fieldList_) {
      if (!field->IsStatic()) {
        SetFocus(field);
        return true;
      }
    }
    return false;
  }

  const int32_t focusX = focus->GetPosition()._x;
  const int32_t focusY = focus->GetPosition()._y;
  UIField *target = nullptr;

  if (yDirection != 0) {
    for (auto *field : fieldList_) {
      if (field->IsStatic() || field->GetPosition()._x != focusX) {
        continue;
      }

      const int32_t y = field->GetPosition()._y;
      if (yDirection > 0 && y > focusY &&
          (!target || y < target->GetPosition()._y)) {
        target = field;
      } else if (yDirection < 0 && y < focusY &&
                 (!target || y > target->GetPosition()._y)) {
        target = field;
      }
    }
  } else if (xDirection != 0) {
    int32_t targetX = 0;
    bool foundColumn = false;
    for (auto *field : fieldList_) {
      if (field->IsStatic()) {
        continue;
      }

      const int32_t x = field->GetPosition()._x;
      if (xDirection > 0 && x > focusX && (!foundColumn || x < targetX)) {
        targetX = x;
        foundColumn = true;
      } else if (xDirection < 0 && x < focusX &&
                 (!foundColumn || x > targetX)) {
        targetX = x;
        foundColumn = true;
      }
    }

    if (foundColumn) {
      int32_t bestDistance = std::numeric_limits<int32_t>::max();
      for (auto *field : fieldList_) {
        if (field->IsStatic() || field->GetPosition()._x != targetX) {
          continue;
        }

        const int32_t distance = std::abs(field->GetPosition()._y - focusY);
        if (!target || distance < bestDistance) {
          target = field;
          bestDistance = distance;
        }
      }
    }
  }

  if (!target) {
    return false;
  }

  SetFocus(target);
  isDirty_ = true;
  return true;
}

bool VoiceView::processNavigationButtonMask(unsigned short mask) {
  bool handled = false;

  if (mask & EPBM_DOWN) {
    handled = moveFocus(0, 1) || handled;
  }
  if (mask & EPBM_UP) {
    handled = moveFocus(0, -1) || handled;
  }
  if (mask & EPBM_RIGHT) {
    handled = moveFocus(1, 0) || handled;
  }
  if (mask & EPBM_LEFT) {
    handled = moveFocus(-1, 0) || handled;
  }

  return handled;
}

void VoiceView::ProcessButtonMask(unsigned short mask, bool pressed) {
  Player *player = Player::GetInstance();

  if (!pressed && previewMode_ != PreviewMode::None) {
    const bool previewHeld =
        previewMode_ == PreviewMode::Parameter
            ? (mask & EPBM_ENTER) != 0
            : IsInstrumentPreviewComboHeld(mask, EPBM_PLAY, EPBM_ALT);
    if (!previewHeld) {
      stopVoicePreview();
      return;
    }
  }

  const unsigned short arrowMask = EPBM_DOWN | EPBM_UP | EPBM_RIGHT | EPBM_LEFT;
  const unsigned short modifierMask =
      EPBM_ENTER | EPBM_EDIT | EPBM_ALT | EPBM_NAV | EPBM_SELECT | EPBM_PLAY;
  const bool plainNavigation =
      (mask & arrowMask) != 0 && (mask & modifierMask) == 0;
  if (plainNavigation) {
    if (pressed) {
      processNavigationButtonMask(mask);
      updateGraphSelectionFromFocus();

      UIField *focusedField = GetFocus();
      if (focusedField && !focusedField->IsStatic()) {
        lastFocusIndex_ = GetFocusIndex();
      }
    }
    return;
  }

  if (pressed && startVoicePreview(mask)) {
    return;
  }

  // Handle EDIT+LEFT/RIGHT to switch between instruments
  if (pressed && (mask & EPBM_EDIT) && (mask & (EPBM_LEFT | EPBM_RIGHT))) {
    warpToNext((mask & EPBM_LEFT) ? -1 : +1);
    return;
  }

  VoiceParameters *voiceParameters = nullptr;
  VoiceParameters::ValueSnapshot previousValues{};
  if (pressed && (mask & (EPBM_ENTER | EPBM_EDIT))) {
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    voiceParameters =
        bank ? bank->GetVoiceParameters(viewData_->currentInstrumentID_)
             : nullptr;
    if (voiceParameters) {
      voiceParameters->CaptureValues(previousValues);
    }
  }

  FieldView::ProcessButtonMask(mask, pressed);

  if (voiceParameters && voiceParameters->ValuesDifferFrom(previousValues)) {
    auditionParameterChange();
  }

  if (!pressed) {
    return;
  }

  updateGraphSelectionFromFocus();

  if (mask & EPBM_NAV) {
    if (mask & EPBM_LEFT) {
      ViewType vt = VT_INSTRUMENT;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
      return;
    }

    if (mask & EPBM_PLAY) {
      player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                            viewData_->chainRow_);
    }
  } else if (mask & EPBM_PLAY) {
    player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                          viewData_->chainRow_);
  }

  UIField *focusedField = GetFocus();
  if (focusedField && !focusedField->IsStatic()) {
    lastFocusIndex_ = GetFocusIndex();
  }
}

void VoiceView::DrawView() {
  Clear();

  GUITextProperties props;

  // Draw title
  etl::string<23> title("Voice ");
  etl::format_spec format;
  format.width(2).fill('0').hex().upper_case(true);
  etl::to_string(viewData_->currentInstrumentID_, title, format, true);
  setTitle(title);

  // Draw directly instead of registering a UIField; this keeps the graph
  // visible while excluding it from focus/edit navigation.
  drawEnvelopeGraphs();

  if (!fieldList_.empty()) {
    FieldView::Redraw();
  }
  drawSectionHeaders();
  drawLFOTargetMarkers();
  drawMap();
}

void VoiceView::OnFocus() {
  if (!SupportsAudioVoiceScreen(getInstrument())) {
    ViewType vt = VT_INSTRUMENT;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }

  refreshVoiceFields();
}

void VoiceView::drawSectionHeaders() {
  GUITextProperties props;
  const GUIPoint anchor = GetAnchor();
  const int32_t topBaseY = anchor._y + 3;
  const int32_t bottomBaseY = topBaseY + 9;
  const int32_t fxBaseY = bottomBaseY - 3;
  const int32_t lfoBaseY = bottomBaseY + 1;

  SetColor(CD_HILITE2);
  DrawRegionString(TEXT_REGION_BODY, VoiceColumnX, topBaseY, "Voice", props);
  DrawRegionString(TEXT_REGION_BODY, FilterColumnX, topBaseY, "Filter", props);
  DrawRegionString(TEXT_REGION_BODY, AmpColumnX, topBaseY, "Amp", props);
  DrawRegionString(TEXT_REGION_BODY, VoiceColumnX, fxBaseY, "Fx", props);
  DrawRegionString(TEXT_REGION_BODY, FilterColumnX, lfoBaseY, "LFO1", props);
  DrawRegionString(TEXT_REGION_BODY, AmpColumnX, lfoBaseY, "LFO2", props);
}

void VoiceView::drawLFOTargetMarkers() {
  I_Instrument *instrument = getInstrument();
  if (!SupportsAudioVoiceScreen(instrument)) {
    return;
  }

  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  if (!bank) {
    return;
  }

  VoiceParameters *voiceParams =
      bank->GetVoiceParameters(viewData_->currentInstrumentID_);
  if (!voiceParams) {
    return;
  }

  LFOSettings *lfoSettings = voiceParams->GetLFOSettings();
  if (!lfoSettings) {
    return;
  }

  GUITextProperties props(true);
  const GUIPoint anchor = GetAnchor();

  SetColor(CD_HILITE2);
  for (size_t i = 0; i < LFOSettings::MaxLFOCount; ++i) {
    GUIPoint position;
    if (!getLFOTargetFieldPosition(lfoSettings->Get(i).target_, anchor,
                                   position)) {
      continue;
    }

    char marker[2] = {static_cast<char>('1' + i), '\0'};
    DrawRegionString(TEXT_REGION_BODY, position._x - 1, position._y, marker,
                     props);
  }
}

void VoiceView::drawEnvelopeGraphs() {
  I_Instrument *instrument = getInstrument();
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  const int32_t instrumentID = viewData_->currentInstrumentID_;
  constexpr ColorDefinition frameColor = CD_NORMAL;
  constexpr ColorDefinition baseGraphColor = CD_EMPHASIS;

  drawEnvelopeBackground(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight);
  drawEnvelopeFrame(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight,
                    frameColor);

  if (!instrument || !bank) {
    return;
  }

  VoiceEnvelopeShapeValues filter = {
      .attack = voiceIntValue(bank, instrument, instrumentID,
                              FourCC::VoiceFilterAttack, 0, 0, 0xFF),
      .decay = voiceIntValue(bank, instrument, instrumentID,
                             FourCC::VoiceFilterDecay, 0, 0, 0xFF),
      .sustain = voiceIntValue(bank, instrument, instrumentID,
                               FourCC::VoiceFilterSustain, 0, 0, 0xFF),
      .release = voiceIntValue(bank, instrument, instrumentID,
                               FourCC::VoiceFilterRelease, 0, 0, 0xFF),
  };

  VoiceEnvelopeShapeValues amp = {
      .attack = voiceIntValue(bank, instrument, instrumentID,
                              FourCC::VoiceAmpAttack, 0, 0, 0xFF),
      .decay = voiceIntValue(bank, instrument, instrumentID,
                             FourCC::VoiceAmpDecay, 0, 0, 0xFF),
      .sustain = voiceIntValue(bank, instrument, instrumentID,
                               FourCC::VoiceAmpSustain, 0xFF, 0, 0xFF),
      .release = voiceIntValue(bank, instrument, instrumentID,
                               FourCC::VoiceAmpRelease, 0, 0, 0xFF),
  };
  const int32_t triggerLength = voiceIntValue(
      bank, instrument, instrumentID, FourCC::VoiceTriggerLength, 0, 0, 96);

  const int32_t filterCutoff = voiceIntValue(
      bank, instrument, instrumentID, FourCC::VoiceFilterCutoff, 0xFF, 0, 0xFF);
  const int32_t filterEnvAmount = voiceIntValue(
      bank, instrument, instrumentID, FourCC::VoiceFilterEnvAmount, 0, -64, 64);
  const int32_t filterEnvDepth =
      std::clamp<int32_t>((filterEnvAmount * 255) / 64, -0xFF, 0xFF);

  const int32_t ampLevel = voiceIntValue(bank, instrument, instrumentID,
                                         FourCC::VoiceAmp, 0xFF, 0, 0xFF);
  const int32_t base = voiceIntValue(bank, instrument, instrumentID,
                                     FourCC::VoiceBandpassCutoff, 0, 0, 0xFF);
  const int32_t widthValue = voiceIntValue(
      bank, instrument, instrumentID, FourCC::VoiceBandpassMix, 0xFF, 0, 0xFF);
  const int32_t pan = voiceIntValue(bank, instrument, instrumentID,
                                    FourCC::VoicePan, 0x80, 0, 0xFF);
  InsertGraphSlot insertSlots[2] = {
      {
          .type = voiceIntValue(
              bank, instrument, instrumentID, GetVoiceInsertTypeFourCC(0),
              kDefaultVoiceInsertEffectIndex, 0, kVoiceInsertEffectCount - 1),
          .amount = voiceIntValue(bank, instrument, instrumentID,
                                  GetVoiceInsertAmountFourCC(0),
                                  kDefaultVoiceInsertAmount, 0, 0xFF),
          .position = voiceIntValue(
              bank, instrument, instrumentID, GetVoiceInsertPositionFourCC(0),
              kDefaultVoiceInsertPosition, 0, kVoiceInsertPositionCount - 1),
      },
      {
          .type = voiceIntValue(
              bank, instrument, instrumentID, GetVoiceInsertTypeFourCC(1),
              kDefaultVoiceInsertEffectIndex, 0, kVoiceInsertEffectCount - 1),
          .amount = voiceIntValue(bank, instrument, instrumentID,
                                  GetVoiceInsertAmountFourCC(1),
                                  kDefaultVoiceInsertAmount, 0, 0xFF),
          .position = voiceIntValue(
              bank, instrument, instrumentID, GetVoiceInsertPositionFourCC(1),
              kDefaultVoiceInsertPosition, 0, kVoiceInsertPositionCount - 1),
      },
  };

  if (graphKind_ == GraphKind::LFO1 || graphKind_ == GraphKind::LFO2) {
    VoiceParameters *voiceParams = SupportsAudioVoiceScreen(instrument)
                                       ? bank->GetVoiceParameters(instrumentID)
                                       : nullptr;
    if (voiceParams) {
      LFOSettings *lfoSettings = voiceParams->GetLFOSettings();
      drawLFOShape(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight,
                   lfoSettings->Get(graphKind_ == GraphKind::LFO1 ? 0 : 1),
                   baseGraphColor);
    }
  } else if (graphKind_ == GraphKind::Voice) {
    if (graphSection_ == GraphSection::TriggerLength) {
      drawTriggerLengthShape(*this, 0, 0, EnvelopePanelWidth,
                             EnvelopeGraphHeight, triggerLength,
                             baseGraphColor);
    } else if (graphSection_ == GraphSection::Pan) {
      drawPanShape(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight, pan,
                   baseGraphColor);
    } else {
      drawBaseWidthShape(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight,
                         base, widthValue, baseGraphColor);
    }
  } else if (graphKind_ == GraphKind::Fx) {
    drawInsertFlowShape(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight,
                        insertSlots, graphSection_, baseGraphColor);
  } else if (graphKind_ == GraphKind::Filter) {
    const bool showFilterResponse =
        graphSection_ == GraphSection::FilterCutoff ||
        graphSection_ == GraphSection::FilterType ||
        graphSection_ == GraphSection::FilterResonance;

    if (showFilterResponse) {
      drawFilterResponseShape(
          *this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight, filterCutoff,
          voiceIntValue(bank, instrument, instrumentID,
                        FourCC::VoiceFilterResonance, 0, 0, 0xFF),
          voiceIntValue(bank, instrument, instrumentID, FourCC::VoiceFilterType,
                        0, 0, 0xFF),
          baseGraphColor);
    } else {
      drawEnvelopeShape(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight,
                        filter, filterEnvDepth, baseGraphColor);
    }
  } else if (graphKind_ == GraphKind::Amp) {
    drawEnvelopeShape(*this, 0, 0, EnvelopePanelWidth, EnvelopeGraphHeight, amp,
                      ampLevel, baseGraphColor);
  }
}
