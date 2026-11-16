/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 xiphonics, inc.
 */
#include "KeyboardModalView.h"
#include "Application/AppWindow.h"
#include "MessageBox.h"
#include <algorithm>
#include <new>

namespace {
constexpr uint8_t keyRowLengths[] = {10, 10, 10, 10};
constexpr uint8_t actionRowLength = 6;
constexpr uint8_t confirmRowLength = 2;
constexpr const char *uppercaseRows[] = {"1234567890", "QWERTYUIOP",
                                         "ASDFGHJKL-", "ZXCVBNM_.'"};
constexpr const char *lowercaseRows[] = {"1234567890", "qwertyuiop",
                                         "asdfghjkl-", "zxcvbnm_.'"};
constexpr const char *symbolRows[] = {"!@#$%^&*()", "[]{}<>?/\\|",
                                      ":;\"'`~+=_-", ",.-!?@#$%&"};
constexpr uint8_t dialogWidth = 24;
constexpr uint8_t actionPositions[] = {0, 5, 9, 13, 15, 21};
constexpr const char *actions[] = {"CASE", "SYM", "DEL", "<", "SPACE", ">"};
#if defined(ADV) && !defined(HOST_TEST) && !defined(WASM)
__attribute__((section(".RAM_EXT")))
#endif
alignas(KeyboardModalView) uint8_t storage[sizeof(KeyboardModalView)];
} // namespace

bool KeyboardModalView::inUse_ = false;

KeyboardModalView *KeyboardModalView::Create(View &view, const char *title,
                                             const char *text,
                                             uint8_t maxLength,
                                             bool projectName) {
  if (inUse_) {
    reinterpret_cast<KeyboardModalView *>(storage)->~KeyboardModalView();
  }
  inUse_ = true;
  return new (storage)
      KeyboardModalView(view, title, text, maxLength, projectName);
}

KeyboardModalView::KeyboardModalView(View &view, const char *title,
                                     const char *text, uint8_t maxLength,
                                     bool projectName)
    : ModalView(view), title_(title),
      maxLength_(std::min<uint8_t>(maxLength, MAX_VARIABLE_STRING_LENGTH)),
      projectName_(projectName) {
  text_.assign(text, std::min<size_t>(strlen(text), maxLength_));
  cursor_ = text_.size();
  row_ = 1;
}

void KeyboardModalView::Destroy() {
  this->~KeyboardModalView();
  inUse_ = false;
}

void KeyboardModalView::DrawView() {
  SetWindow(dialogWidth, 16, TEXT_FONT_SMALL);
  GUITextProperties props;
  SetColor(CD_ACCENT);
  const size_t titleOffset =
      title_.size() <= dialogWidth ? (dialogWidth - title_.size()) / 2 : 0;
  DrawString(titleOffset, 0, title_.c_str(), props);

  // Scroll longer future text fields while keeping the insertion cursor
  // visible.
  const uint8_t start = cursor_ >= 22 ? cursor_ - 21 : 0;
  etl::string<24> visible(text_.substr(start, 22));
  visible += ' ';
  SetColor(CD_EMPHASIS);
  DrawString(1, 2, visible.c_str(), props);
  etl::string<1> cursorText;
  cursorText += cursor_ < text_.size() ? text_[cursor_] : ' ';
  props.invert_ = true;
  DrawString(1 + cursor_ - start, 2, cursorText.c_str(), props);
  props.invert_ = false;
  if (!error_.empty()) {
    SetColor(CD_ERROR);
    DrawString((dialogWidth - error_.size()) / 2, 3, error_.c_str(), props);
  }

  // Shared box-drawing borders create compact, individually framed QWERTY
  // keys while keeping a full ten-key row inside the modal width.
  SetColor(CD_ACCENT);
  for (uint8_t boundary = 0; boundary <= 4; ++boundary) {
    etl::string<21> border;
    for (uint8_t column = 0; column < 10; ++column) {
      const uint8_t junction = column == 0 ? (boundary == 0   ? 218
                                              : boundary == 4 ? 192
                                                              : 195)
                                           : (boundary == 0   ? 194
                                              : boundary == 4 ? 193
                                                              : 197);
      border += static_cast<char>(junction);
      border += static_cast<char>(196);
    }
    border += static_cast<char>(boundary == 0   ? 191
                                : boundary == 4 ? 217
                                                : 180);
    SetColor(CD_ACCENT);
    DrawString((dialogWidth - 21) / 2, 4 + boundary * 2, border.c_str(), props);
  }
  for (uint8_t row = 0; row < 4; ++row) {
    const char *characters =
        symbols_ ? symbolRows[row]
                 : (lowercase_ ? lowercaseRows[row] : uppercaseRows[row]);
    const uint8_t width = keyRowLengths[row] * 2 + 1;
    const uint8_t startX = (dialogWidth - width) / 2;
    const uint8_t middleY = 5 + row * 2;
    for (uint8_t column = 0; column < keyRowLengths[row]; ++column) {
      SetColor(CD_INFO);
      etl::string<2> key;
      key += static_cast<char>(179);
      key += characters[column];
      DrawString(startX + column * 2, middleY, key.c_str(), props);
    }
    SetColor(CD_ACCENT);
    etl::string<1> rightBorder;
    rightBorder += static_cast<char>(179);
    DrawString(startX + width - 1, middleY, rightBorder.c_str(), props);
  }
  for (uint8_t column = 0; column < actionRowLength; ++column) {
    const char *label = actions[column];
    if (column == 0 && lowercase_) {
      label = "UPPER";
    } else if (column == 1 && symbols_) {
      label = "ABC";
    }
    props.invert_ = false;
    const bool selected = row_ == 4 && column_ == column;
    const bool active =
        (column == 0 && lowercase_) || (column == 1 && symbols_);
    SetColor(selected ? CD_HILITE2 : active ? CD_INFO : CD_ACCENT);
    DrawString(actionPositions[column], 13, label, props);
  }
  props.invert_ = false;
  SetColor(row_ == 5 && column_ == 0 ? CD_HILITE2 : CD_INFO);
  DrawString(7, 15, "OK", props);
  SetColor(row_ == 5 && column_ == 1 ? CD_HILITE2 : CD_INFO);
  DrawString(14, 15, "CANCEL", props);
  GUIRect outline;
  if (row_ < 4) {
    const GUIPoint anchor = GetAnchor();
    const int32_t left = anchor._x + (dialogWidth - 21) / 2 + column_ * 2;
    const int32_t top = anchor._y + 4 + row_ * 2;
    outline = GUIRect(left, top, left + 3, top + 3);
  }
  static_cast<AppWindow &>(w_).SetOverlayOutline(outline, CD_EMPHASIS);
}

void KeyboardModalView::ActivateKey() {
  error_.clear();
  if (row_ == 5 && column_ == 1) {
    EndModal(MBL_CANCEL);
    return;
  }
  if (row_ == 5 && column_ == 0) {
    if (text_.empty() || text_.find_first_not_of(' ') == etl::istring::npos) {
      error_ = projectName_ ? "ENTER A PROJECT NAME" : "ENTER TEXT";
    } else if (projectName_ && !IsValidProjectName()) {
      error_ = "NO TRAILING DOT OR SPACE";
    } else {
      EndModal(MBL_OK);
    }
    return;
  }
  if (row_ == 4) {
    switch (column_) {
    case 0:
      lowercase_ = !lowercase_;
      return;
    case 1:
      symbols_ = !symbols_;
      lowercase_ = false;
      column_ = 0;
      row_ = 0;
      return;
    case 2:
      if (cursor_ > 0) {
        text_.erase(--cursor_, 1);
      }
      return;
    case 4:
      if (text_.size() < maxLength_) {
        text_.insert(cursor_++, 1, ' ');
      } else {
        error_ = "MAXIMUM LENGTH REACHED";
      }
      return;
    case 3:
      if (cursor_ > 0) {
        --cursor_;
      }
      return;
    case 5:
      if (cursor_ < text_.size()) {
        ++cursor_;
      }
      return;
    }
  }
  if (text_.size() < maxLength_) {
    const char character =
        symbols_
            ? symbolRows[row_][column_]
            : (lowercase_ ? lowercaseRows[row_] : uppercaseRows[row_])[column_];
    text_.insert(cursor_++, 1, character);
  } else {
    error_ = "MAXIMUM LENGTH REACHED";
  }
}

bool KeyboardModalView::IsValidProjectName() const {
  if (text_ == "." || text_ == "..") {
    return false;
  }
  const char last = text_.back();
  return last != ' ' && last != '.';
}

void KeyboardModalView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed) {
    return;
  }
  if (mask == EPBM_ENTER) {
    ActivateKey();
  } else if (!(mask & (EPBM_ENTER | EPBM_EDIT | EPBM_NAV | EPBM_ALT |
                       EPBM_SELECT | EPBM_PLAY))) {
    if (mask & EPBM_UP) {
      row_ = row_ == 0 ? 5 : row_ - 1;
      if (row_ == 4) {
        column_ = actionRowLength - 1;
      } else if (row_ == 5) {
        column_ = 0;
      }
    } else if (mask & EPBM_DOWN) {
      row_ = (row_ + 1) % 6;
      if (row_ == 4) {
        column_ = std::min<uint8_t>(column_, actionRowLength - 1);
      } else if (row_ == 5) {
        column_ = column_ <= 2 ? 0 : 1;
      }
    } else if (mask & EPBM_LEFT) {
      const uint8_t count = row_ == 5   ? confirmRowLength
                            : row_ == 4 ? actionRowLength
                                        : keyRowLengths[row_];
      column_ = column_ == 0 ? count - 1 : column_ - 1;
    } else if (mask & EPBM_RIGHT) {
      const uint8_t count = row_ == 5   ? confirmRowLength
                            : row_ == 4 ? actionRowLength
                                        : keyRowLengths[row_];
      column_ = (column_ + 1) % count;
    }
    const uint8_t count = row_ == 5   ? confirmRowLength
                          : row_ == 4 ? actionRowLength
                                      : keyRowLengths[row_];
    column_ = std::min<uint8_t>(column_, count - 1);
  }
  if (mask & (EPBM_UP | EPBM_DOWN | EPBM_LEFT | EPBM_RIGHT)) {
    error_.clear();
  }
  isDirty_ = true;
}
