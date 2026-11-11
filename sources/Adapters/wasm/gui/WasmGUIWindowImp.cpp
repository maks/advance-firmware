/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024-2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "WasmGUIWindowImp.h"
#include "Adapters/adv/display/display.h"
#include "Adapters/wasm/display/WasmDisplayBackend.h"
#include "Adapters/wasm/input/InputMap.h"
#include "System/Console/Trace.h"
#include "System/RemoteUI/RemoteUIProtocol.h"
#include "System/System/System.h"
#include "UIFramework/BasicDatas/TextFont.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

#include <GLES2/gl2.h>

namespace {
struct GlyphPaint {
  const uint8_t *glyph;
  int32_t x;
  int32_t y;
  uint8_t width;
  uint8_t height;
  uint16_t foreground;
  uint16_t background;
};

uint16_t BlendColor565(uint16_t foreground, uint16_t background,
                       uint8_t alpha) {
  const uint32_t inverseAlpha = 255U - alpha;
  const uint32_t foregroundRed = (foreground & 0x1FU) << 3U;
  const uint32_t foregroundGreen = ((foreground >> 5U) & 0x3FU) << 2U;
  const uint32_t foregroundBlue = ((foreground >> 11U) & 0x1FU) << 3U;
  const uint32_t backgroundRed = (background & 0x1FU) << 3U;
  const uint32_t backgroundGreen = ((background >> 5U) & 0x3FU) << 2U;
  const uint32_t backgroundBlue = ((background >> 11U) & 0x1FU) << 3U;
  const uint32_t red =
      (foregroundRed * alpha + backgroundRed * inverseAlpha + 127U) / 255U;
  const uint32_t green =
      (foregroundGreen * alpha + backgroundGreen * inverseAlpha + 127U) / 255U;
  const uint32_t blue =
      (foregroundBlue * alpha + backgroundBlue * inverseAlpha + 127U) / 255U;
  return static_cast<uint16_t>(((blue >> 3U) << 11U) | ((green >> 2U) << 5U) |
                               (red >> 3U));
}

void PaintGlyph(void *context, GraphicsRegion clip, uint16_t *destination,
                uint16_t stride, bool /*visible*/) {
  const auto &paint = *static_cast<GlyphPaint *>(context);
  for (int32_t row = 0; row < clip.height; ++row) {
    const int32_t sourceY = clip.y - paint.y + row;
    const int32_t sourceX = clip.x - paint.x;
    const uint8_t *source = paint.glyph + sourceY * paint.width + sourceX;
    uint16_t *target = destination + row * stride;
    for (int32_t column = 0; column < clip.width; ++column) {
      const uint8_t alpha = source[column];
      if (alpha == 0) {
        target[column] = paint.background;
      } else if (alpha == 255) {
        target[column] = paint.foreground;
      } else {
        target[column] =
            BlendColor565(paint.foreground, paint.background, alpha);
      }
    }
  }
}

uint16_t ToBgr565(uint16_t rgb565) {
  return static_cast<uint16_t>(((rgb565 & 0x001FU) << 11U) |
                               (rgb565 & 0x07E0U) |
                               ((rgb565 >> 11U) & 0x001FU));
}

void RenderGlyph(const TextFontInfo &font, uint8_t character, int32_t x,
                 int32_t y, uint16_t foreground, uint16_t background) {
  if (!font.pixels || character < 32)
    return;

  uint16_t glyphIndex = static_cast<uint16_t>(character - 32U);
  if (glyphIndex >= font.count)
    glyphIndex = static_cast<uint16_t>('?' - 32);

  GlyphPaint paint = {
      .glyph = font.pixels + glyphIndex * font.width * font.height,
      .x = x,
      .y = y,
      .width = font.width,
      .height = font.height,
      .foreground = ToBgr565(foreground),
      .background = ToBgr565(background),
  };
  const GraphicsRegion bounds = {static_cast<int16_t>(x),
                                 static_cast<int16_t>(y), font.width,
                                 font.height};
  display_visit_pixels(bounds, true, PaintGlyph, &paint);
  WasmDisplayBackend::MarkDirty();
}

constexpr char VertexShaderSource[] = R"(
attribute vec2 a_position;
attribute vec2 a_texcoord;
varying vec2 v_texcoord;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_texcoord = a_texcoord;
}
)";

constexpr char FragmentShaderSource[] = R"(
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_texcoord;
void main() {
  gl_FragColor = texture2D(u_texture, v_texcoord);
}
)";

unsigned int CompileShader(unsigned int type, const char *source) {
  const unsigned int shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  int compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == 0) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}
} // namespace

WasmGUIWindowImp *WasmGUIWindowImp::instance_ = nullptr;

WasmGUIWindowImp::WasmGUIWindowImp(GUICreateWindowParams & /*p*/) {
  instance_ = this;
  rgbaFrame_.fill(0);
  for (size_t i = 3; i < rgbaFrame_.size(); i += 4) {
    rgbaFrame_[i] = 0xFF;
  }

  InitializePresenter();
}

WasmGUIWindowImp::~WasmGUIWindowImp() {
  if (instance_ == this) {
    instance_ = nullptr;
  }
  DestroyPresenter();
}

WasmGUIWindowImp *WasmGUIWindowImp::GetInstance() { return instance_; }

bool WasmGUIWindowImp::HasPresentedFrame() const { return hasPresentedFrame_; }

bool WasmGUIWindowImp::InitializePresenter() {
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = EM_FALSE;
  attributes.antialias = EM_FALSE;
  attributes.depth = EM_FALSE;
  attributes.stencil = EM_FALSE;
  attributes.explicitSwapControl = EM_TRUE;
  attributes.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
  attributes.majorVersion = 1;
  context_ =
      emscripten_webgl_create_context("#picotracker-canvas", &attributes);
  if (context_ <= 0 || emscripten_webgl_make_context_current(context_) !=
                           EMSCRIPTEN_RESULT_SUCCESS) {
    context_ = 0;
    return false;
  }

  const unsigned int vertexShader =
      CompileShader(GL_VERTEX_SHADER, VertexShaderSource);
  const unsigned int fragmentShader =
      CompileShader(GL_FRAGMENT_SHADER, FragmentShaderSource);
  if (vertexShader == 0 || fragmentShader == 0) {
    if (vertexShader)
      glDeleteShader(vertexShader);
    if (fragmentShader)
      glDeleteShader(fragmentShader);
    DestroyPresenter();
    return false;
  }

  shaderProgram_ = glCreateProgram();
  glAttachShader(shaderProgram_, vertexShader);
  glAttachShader(shaderProgram_, fragmentShader);
  glLinkProgram(shaderProgram_);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  int linked = 0;
  glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &linked);
  if (linked == 0) {
    DestroyPresenter();
    return false;
  }

  positionLocation_ = glGetAttribLocation(shaderProgram_, "a_position");
  textureLocation_ = glGetAttribLocation(shaderProgram_, "a_texcoord");
  if (positionLocation_ < 0 || textureLocation_ < 0) {
    DestroyPresenter();
    return false;
  }

  constexpr float vertices[] = {
      -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
      -1.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 0.0f,
  };
  glGenBuffers(1, &vertexBuffer_);
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CanvasWidth, CanvasHeight, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);

  return glGetError() == GL_NO_ERROR;
}

void WasmGUIWindowImp::DestroyPresenter() {
  if (context_ > 0) {
    emscripten_webgl_make_context_current(context_);
    if (texture_ != 0) {
      glDeleteTextures(1, &texture_);
    }
    if (vertexBuffer_ != 0) {
      glDeleteBuffers(1, &vertexBuffer_);
    }
    if (shaderProgram_ != 0) {
      glDeleteProgram(shaderProgram_);
    }
    emscripten_webgl_destroy_context(context_);
  }
  context_ = 0;
  shaderProgram_ = 0;
  texture_ = 0;
  vertexBuffer_ = 0;
}

bool WasmGUIWindowImp::PresentFrame(const RgbaFrame &frame) {
  if (context_ <= 0) {
    return false;
  }
  if (emscripten_webgl_make_context_current(context_) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    return false;
  }

  glViewport(0, 0, CanvasWidth, CanvasHeight);
  glUseProgram(shaderProgram_);
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
  glEnableVertexAttribArray(static_cast<unsigned int>(positionLocation_));
  glEnableVertexAttribArray(static_cast<unsigned int>(textureLocation_));
  glVertexAttribPointer(static_cast<unsigned int>(positionLocation_), 2,
                        GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glVertexAttribPointer(static_cast<unsigned int>(textureLocation_), 2,
                        GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<const void *>(2 * sizeof(float)));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CanvasWidth, CanvasHeight, GL_RGBA,
                  GL_UNSIGNED_BYTE, frame.data());
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  emscripten_webgl_commit_frame();
  hasPresentedFrame_ = true;
  return true;
}

void WasmGUIWindowImp::DrawChar(const char c, const GUIPoint &pos,
                                const GUITextProperties &p) {
  const uint8_t character = static_cast<uint8_t>(c);
  const uint8_t target = display_target();
  if (target != 0) {
    if (pos._x < 0 || pos._x >= 32 || pos._y < 0 || pos._y >= 24)
      return;
    const TextFontInfo *font = text_font(TEXT_FONT_NORMAL);
    if (!font)
      return;
    const uint16_t foreground =
        p.invert_ ? backgroundColor565_ : foregroundColor565_;
    const uint16_t background =
        p.invert_ ? foregroundColor565_ : backgroundColor565_;
    RenderGlyph(*font, character, 8 + pos._x * font->width,
                pos._y * font->height, foreground, background);
    return;
  }

  const TextRegionLayout *layout = display_text_layout();
  if (p.region_ >= layout->count || pos._x < 0 || pos._y < 0)
    return;
  const TextRegionInfo &region = layout->regions[p.region_];
  if (pos._x >= region.columns || pos._y >= region.rows)
    return;
  const TextFontInfo *font = text_font(region.definition.font);
  if (!font)
    return;

  const uint16_t foreground =
      p.invert_ ? backgroundColor565_ : foregroundColor565_;
  const uint16_t background =
      p.invert_ ? foregroundColor565_ : backgroundColor565_;
  const int32_t x = region.definition.x + pos._x * region.glyph_width;
  const int32_t y = region.definition.y + pos._y * region.glyph_height;
  RenderGlyph(*font, character, x, y, foreground, background);
}

void WasmGUIWindowImp::DrawString(const char *string, const GUIPoint &pos,
                                  const GUITextProperties &p,
                                  bool /*overlay*/) {
  if (!string)
    return;
  GUIPoint characterPos(pos);
  while (*string) {
    DrawChar(*string++, characterPos, p);
    ++characterPos._x;
  }
}

void WasmGUIWindowImp::DrawRect(GUIRect &r) {
  display_fill_rect(lastColorIdx_, static_cast<uint16_t>(r.Left()),
                    static_cast<uint16_t>(r.Top()),
                    static_cast<uint16_t>(r.Width()),
                    static_cast<uint16_t>(r.Height()));
}

color_t WasmGUIWindowImp::GetColor(GUIColor &c) {
  display_set_palette_color(c._paletteIndex, to_rgb565(c));
  return static_cast<color_t>(c._paletteIndex);
}

void WasmGUIWindowImp::Clear(GUIColor &c, bool /*overlay*/) {
  const color_t bg = GetColor(c);
  backgroundColor565_ = to_rgb565(c);
  display_set_background(bg);
  display_clear(bg);
}

void WasmGUIWindowImp::ClearTextRect(GUIRect & /*r*/) {}

void WasmGUIWindowImp::SetColor(GUIColor &c) {
  const color_t color = GetColor(c);
  lastColorIdx_ = static_cast<uint8_t>(color);
  foregroundColor565_ = to_rgb565(c);
  display_set_foreground(color);
}

void WasmGUIWindowImp::Lock() {}
void WasmGUIWindowImp::Unlock() {}

void WasmGUIWindowImp::Flush() {
  display_draw_changed();
  if (WasmDisplayBackend::IsDirty()) {
    if (context_ > 0) {
      WasmDisplayBackend::CaptureFrameRgba(rgbaFrame_.data(),
                                           rgbaFrame_.size());
      PresentFrame(rgbaFrame_);
    }
    WasmDisplayBackend::ClearDirty();
  }
}

void WasmGUIWindowImp::Invalidate() {
  if (_window) {
    _window->Update(false);
  }
}

void WasmGUIWindowImp::PushEvent(GUIEvent & /*event*/) {}

GUIRect WasmGUIWindowImp::GetRect() { return GUIRect(0, 0, 320, 240); }

void WasmGUIWindowImp::ClockTick() {
  if (_window) {
    _window->ClockTick();
  }
}

void WasmGUIWindowImp::UpdateFrame(bool redraw) {
  if (_window) {
    _window->Update(redraw);
  }
}

void WasmGUIWindowImp::ProcessButtonChange(uint16_t changeMask,
                                           uint16_t buttonMask) {
  if (!instance_ || !instance_->_window) {
    return;
  }
  int e = 1;
  System *system = System::GetInstance();
  const unsigned long now = system ? system->GetClock() : 0;
  for (uint32_t i = 0;
       i < sizeof(eventMappingPico) / sizeof(eventMappingPico[0]); i++) {
    if (changeMask & e) {
      const GUIEventType type =
          (buttonMask & e) ? ET_PADBUTTONDOWN : ET_PADBUTTONUP;
      GUIEvent event(eventMappingPico[i], type, now, 0, 0, 0);
      instance_->_window->DispatchEvent(event);
    }
    e = e << 1;
  }
}
