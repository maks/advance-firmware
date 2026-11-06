/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "UIFM6AlgoField.h"

#include "Application/AppWindow.h"
#include "ViewUtils.h"
#include <System/Console/nanoprintf.h>
#include <algorithm>
#include <string.h>

namespace {

// Modulator->carrier chains for the 32 DX7 algorithms, generated from the
// dx7Algorithms bus-flag table in FM6Voice.cpp: deepest modulator first,
// '+' sums sibling modulators, '*' marks the feedback operator, chains
// listed by ascending carrier.
const char *const kAlgoChains[32] = {
    "2>1 6*>5>4>3",       // 1
    "2*>1 6>5>4>3",       // 2
    "3>2>1 6*>5>4",       // 3
    "3>2>1 6>5>4*",       // 4
    "2>1 4>3 6*>5",       // 5
    "2>1 4>3 6>5*",       // 6
    "2>1 4+6*>5>3",       // 7
    "2>1 4*+6>5>3",       // 8
    "2*>1 4+6>5>3",       // 9
    "3*>2>1 5+6>4",       // 10
    "3>2>1 5+6*>4",       // 11
    "2*>1 4+5+6>3",       // 12
    "2>1 4+5+6*>3",       // 13
    "2>1 5+6*>4>3",       // 14
    "2*>1 5+6>4>3",       // 15
    "2+4>3+6*>5>1",       // 16
    "2*+4>3+6>5>1",       // 17
    "2+3*+6>5>4>1",       // 18
    "3>2>1 6*>4 6*>5",    // 19
    "3*>1 3*>2 5+6>4",    // 20
    "3*>1 3*>2 6>4 6>5",  // 21
    "2>1 6*>3 6*>4 6*>5", // 22
    "1 3>2 6*>4 6*>5",    // 23
    "1 2 6*>3 6*>4 6*>5", // 24
    "1 2 3 6*>4 6*>5",    // 25
    "1 3>2 5+6*>4",       // 26
    "1 3*>2 5+6>4",       // 27
    "2>1 5*>4>3 6",       // 28
    "1 2 4>3 6*>5",       // 29
    "1 2 5*>4>3 6",       // 30
    "1 2 3 4 6*>5",       // 31
    "1 2 3 4 5 6*",       // 32
};

constexpr char kArrowGlyph = '\x90';
// Length of the "alg:NN " prefix before the chain text.
constexpr int kChainOffset = 7;
// Prefix, longest chain (19), " G" and the terminator.
constexpr size_t kAlgoBufferSize = 32;

// Fills `order` with the operators of `chain` in the order they appear,
// returning how many there were. Every algorithm uses all six.
uint8_t operatorOrder(const char *chain, uint8_t *order) {
  bool seen[fm6::OperatorCount + 1] = {};
  uint8_t count = 0;
  for (const char *p = chain; *p; ++p) {
    if (*p >= '1' && *p <= '6' && !seen[*p - '0']) {
      seen[*p - '0'] = true;
      order[count++] = static_cast<uint8_t>(*p - '0');
    }
  }
  return count;
}

} // namespace

UIFM6AlgoField::UIFM6AlgoField(const GUIPoint &position,
                               FM6Instrument &instrument, uint8_t &selection)
    : UIField(position), instrument_(instrument), selection_(selection){};

void UIFM6AlgoField::Draw(GUIWindow &w, int offset) {
  GUITextProperties props;
  props.region_ = GetTextRegion();
  GUIPoint position = GetTextPosition();
  position._y += offset;

  const uint8_t algo = instrument_.GetAlgorithm();
  char buffer[kAlgoBufferSize];
  npf_snprintf(buffer, sizeof(buffer), "Alg:%2.2d %s G", algo + 1,
               kAlgoChains[algo]);
  for (char *p = buffer + kChainOffset; *p; ++p) {
    if (*p == '>') {
      *p = kArrowGlyph;
    }
  }

  if (focus_) {
    ((AppWindow &)w).SetColor(CD_HILITE2);
    props.invert_ = true;
    w.DrawString(buffer, position, props);
  } else {
    DrawLabeledField(w, position, buffer, props);
  }

  // The cursor is drawn whether or not this field has focus: once focus
  // moves down into the parameter panel, this marker is the only thing
  // showing which operator (or the global block) those values belong to.
  // Row highlighting means keyboard focus, the marker means selection, so
  // the two use different colours to stay distinguishable when both are on.
  // An operator can appear in more than one chain, so every occurrence of
  // its digit is marked.
  ((AppWindow &)w).SetColor(focus_ ? CD_NORMAL : CD_HILITE2);
  props.invert_ = true;
  const char cursorChar = (selection_ >= FM6SelectionGlobal)
                              ? 'G'
                              : static_cast<char>('0' + selection_);
  char cursor[2] = {cursorChar, 0};
  GUIPoint cursorPosition = position;
  for (const char *p = buffer + kChainOffset; *p; ++p) {
    if (*p == cursorChar) {
      cursorPosition._x = position._x + static_cast<int>(p - buffer);
      w.DrawString(cursor, cursorPosition, props);
    }
  }
};

void UIFM6AlgoField::ProcessArrow(unsigned short mask) {
  const uint8_t algo = instrument_.GetAlgorithm();

  switch (mask) {
  case EPBM_UP:
  case EPBM_DOWN: {
    const int next = std::clamp(algo + ((mask == EPBM_UP) ? 1 : -1), 0, 31);
    if (next != algo) {
      instrument_.SetAlgorithm(static_cast<uint8_t>(next));
      SetChanged();
      NotifyObservers(reinterpret_cast<I_ObservableData *>(
          static_cast<uintptr_t>(FourCC::FM6InstrumentGlobalParams)));
    }
    break;
  }
  case EPBM_LEFT:
  case EPBM_RIGHT: {
    uint8_t order[fm6::OperatorCount];
    const uint8_t count = operatorOrder(kAlgoChains[algo], order);
    // The global block sits one stop past the last operator.
    int index = count;
    for (uint8_t i = 0; i < count; ++i) {
      if (order[i] == selection_) {
        index = i;
        break;
      }
    }
    index = std::clamp(index + ((mask == EPBM_RIGHT) ? 1 : -1), 0,
                       static_cast<int>(count));
    selection_ = (index == count) ? FM6SelectionGlobal : order[index];
    break;
  }
  };
};
