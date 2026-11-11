/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Platform-independent screen layout and overlay state for the Wasm display.
 */

#include "Adapters/adv/display/display.h"
#include "Adapters/adv/display/font.h"
#include "Adapters/adv/system/critical_error_message.h"
#include "Adapters/wasm/display/WasmDisplayBackend.h"
#include "System/Display/Display.h"

#include <cstring>

namespace {

TextRegionLayout textLayout{};
bool textLayoutInitialized = false;
GraphicsRegion graphicsRegions[GRAPHICS_REGION_LIMIT]{};
uint8_t graphicsCount = 0;
uint32_t graphicsRevision = 0;
uint8_t activeTarget = 0;
DisplayOverlayObserver overlayObserver = nullptr;
uint16_t fullscreenOverlay[DISPLAY_WIDTH * DISPLAY_HEIGHT]{};
uint16_t partialOverlay[DISPLAY_WIDTH * 90]{};
PixelTargets pixelTargets = {
    .front = nullptr, .width = DISPLAY_WIDTH, .height = DISPLAY_HEIGHT};

void EnsurePixelTargets() {
  if (pixelTargets.front == nullptr)
    pixelTargets.front = WasmDisplayBackend::GetMutableFramebuffer();
}

void RouteText(uint8_t target, GraphicsRegion bounds, PixelVisitor visitor,
               void *context) {
  if (target == 0) {
    for (uint8_t i = 0; i < graphicsCount; ++i) {
      const GraphicsRegion hit = pixel_intersection(bounds, graphicsRegions[i]);
      if (hit.width && hit.height)
        return;
    }
  }
  pixel_route(&pixelTargets, target, bounds, visitor, context);
}

void CountVisible(void *context, GraphicsRegion region, uint16_t *pixels,
                  uint16_t stride, bool visible) {
  (void)pixels;
  (void)stride;
  if (visible)
    *static_cast<uint32_t *>(context) +=
        static_cast<uint32_t>(region.width) * region.height;
}

} // namespace

extern "C" {

volatile DisplayOverlayDiagnostics gDisplayOverlayDiagnostics{};
const TextFontInfo text_fonts[TEXT_FONT_COUNT] = {
    {&FONT[0][0][0], FONT_CHAR_COUNT, FONT_CHAR_WIDTH, FONT_CHAR_HEIGHT},
    {&FONTSMALL[0][0][0], FONT_CHAR_COUNT, 16, 24},
    {&FONTTINY[0][0][0], FONT_CHAR_COUNT, 10, 15}};

void display_set_overlay_observer(DisplayOverlayObserver observer) {
  overlayObserver = observer;
}

void display_select_target(uint8_t target) { activeTarget = target; }
uint8_t display_target(void) { return activeTarget; }
uint8_t display_overlay_depth(void) { return pixelTargets.depth; }

void display_discard_overlays(void) {
  if (pixelTargets.depth && overlayObserver)
    overlayObserver(DISPLAY_OVERLAY_RESET, (GraphicsRegion){0, 0, 0, 0});
  pixelTargets.depth = 0;
  activeTarget = 0;
}

bool display_push_overlay(GraphicsRegion bounds) {
  EnsurePixelTargets();
  uint16_t *storage = pixelTargets.depth ? partialOverlay : fullscreenOverlay;
  const size_t capacity =
      pixelTargets.depth
          ? sizeof(partialOverlay) / sizeof(partialOverlay[0])
          : sizeof(fullscreenOverlay) / sizeof(fullscreenOverlay[0]);
  const bool captured = pixel_push(&pixelTargets, bounds, storage, capacity);
  if (captured && overlayObserver)
    overlayObserver(DISPLAY_OVERLAY_PUSH, bounds);
  return captured;
}

void display_pop_overlay(void) {
  if (!pixelTargets.depth)
    return;
  const GraphicsRegion bounds =
      pixelTargets.backing[pixelTargets.depth - 1].bounds;
  pixel_pop(&pixelTargets);
  activeTarget = 0;
  WasmDisplayBackend::MarkDirty();
  if (overlayObserver)
    overlayObserver(DISPLAY_OVERLAY_POP, bounds);
}

uint32_t display_graphics_revision(void) { return graphicsRevision; }

GraphicsLayout display_graphics_layout(void) {
  return (GraphicsLayout){graphicsRegions, graphicsCount};
}

void display_visit_pixels(GraphicsRegion bounds, bool text,
                          PixelVisitor visitor, void *context) {
  EnsurePixelTargets();
  if (text)
    RouteText(activeTarget, bounds, visitor, context);
  else
    pixel_route(&pixelTargets, activeTarget, bounds, visitor, context);
}

uint32_t display_visible_area(GraphicsRegion bounds, bool text) {
  EnsurePixelTargets();
  if (!pixelTargets.depth && (!text || !graphicsCount))
    return static_cast<uint32_t>(bounds.width) * bounds.height;
  uint32_t area = 0;
  if (text)
    RouteText(activeTarget, bounds, CountVisible, &area);
  else
    pixel_route(&pixelTargets, activeTarget, bounds, CountVisible, &area);
  return area;
}

bool display_configure_graphics(GraphicsLayout config) {
  if (config.count > GRAPHICS_REGION_LIMIT ||
      (config.count && config.regions == nullptr))
    return false;
  for (uint8_t i = 0; i < config.count; ++i) {
    const GraphicsRegion region = config.regions[i];
    if (region.x < 0 || region.y < 0 || region.width <= 0 ||
        region.height <= 0 || region.x + region.width > DISPLAY_WIDTH ||
        region.y + region.height > DISPLAY_HEIGHT)
      return false;
    for (uint8_t j = 0; j < i; ++j) {
      const GraphicsRegion hit = pixel_intersection(region, config.regions[j]);
      if (hit.width && hit.height)
        return false;
    }
  }
  if (graphicsCount == config.count &&
      (!config.count ||
       std::memcmp(graphicsRegions, config.regions,
                   config.count * sizeof(GraphicsRegion)) == 0))
    return true;

  for (uint8_t i = 0; i < graphicsCount; ++i) {
    const GraphicsRegion region = graphicsRegions[i];
    display_fill_rect(COLOR_BG, region.x, region.y, region.width,
                      region.height);
  }
  ++graphicsRevision;
  graphicsCount = config.count;
  if (config.count)
    std::memcpy(graphicsRegions, config.regions,
                config.count * sizeof(GraphicsRegion));
  return true;
}

bool display_configure_text_regions(const TextRegion *regions, uint8_t count) {
  TextRegionLayout next{};
  if (!text_region_plan(&next, regions, count))
    return false;
  if (textLayoutInitialized &&
      std::memcmp(&textLayout, &next, sizeof(next)) == 0)
    return true;
  graphicsCount = 0;
  textLayout = next;
  textLayoutInitialized = true;
  return true;
}

const TextRegionLayout *display_text_layout(void) {
  if (!textLayoutInitialized) {
    textLayout = {};
    textLayoutInitialized = true;
  }
  return &textLayout;
}

void display_putc_region(uint8_t, uint8_t, uint8_t, char, bool) {}
void display_draw_overlay_char(uint8_t, uint8_t, char, bool) {}

void critical_error_message(const ErrorBitmap *, const char *,
                            const char *const *, size_t, int) {
  __builtin_trap();
}

void critical_sdcard_missing_message(int) { __builtin_trap(); }

} // extern "C"
