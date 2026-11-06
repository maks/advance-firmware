/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _DWT_PROFILER_H_
#define _DWT_PROFILER_H_

#include <cstddef>
#include <cstdint>
#include <limits>

#ifndef ENABLE_PROFILING
#define ENABLE_PROFILING 0
#endif

#include "Application/Model/Song.h"

#if !defined(HOST_TEST) && !defined(WASM) && ENABLE_PROFILING
#include "Adapters/adv/platform/platform.h"
#include "System/Console/Trace.h"
#include "System/Console/nanoprintf.h"
#endif

struct DWTProfileStats {
  const char *name_ = nullptr;
  bool printLast_ = false;
  bool printIndividually_ = true;
  bool flushBoundary_ = false;
  uint16_t displayOrder_ = 0;
  uint32_t budgetPctX1000_ = 0;
  uint64_t totalCycles_ = 0;
  const DWTProfileStats *aggregateChildren_[4] = {};
  uint8_t aggregateChildCount_ = 0;
  DWTProfileStats *next_ = nullptr;
  bool registered_ = false;
};

struct DWTProfileMatrixRow {
  const char *name_ = nullptr;
  DWTProfileStats *columns_[SONG_CHANNEL_COUNT] = {};
  uint32_t budgetPctX1000_ = 0;
};

struct DWTProfileMatrix {
  DWTProfileMatrixRow *rows_ = nullptr;
  uint32_t rowCount_ = 0;
  uint32_t columnCount_ = 0;
  DWTProfileStats *channelTotals_[SONG_CHANNEL_COUNT] = {};
  DWTProfileStats *voicesTotal_ = nullptr;
  uint32_t channelBudgetPctX1000_ = 0;
  uint32_t voicesBudgetPctX1000_ = 0;
};

constexpr uint32_t ComputeDWTBudgetSharePctX1000(uint32_t totalBudgetPctX1000,
                                                 uint32_t slotCount) {
  return slotCount > 0 ? totalBudgetPctX1000 / slotCount : 0;
}

inline uint32_t ComputeDWTAbsolutePercentX1000(uint64_t cycles, uint32_t cpuHz,
                                               uint32_t elapsedUs) {
  if (cpuHz == 0 || elapsedUs == 0) {
    return 0;
  }

  const uint64_t windowCycles =
      (static_cast<uint64_t>(cpuHz) * elapsedUs) / 1000000ULL;
  if (windowCycles == 0) {
    return 0;
  }

  const uint64_t percentX1000 = (cycles * 100000ULL) / windowCycles;
  return percentX1000 > std::numeric_limits<uint32_t>::max()
             ? std::numeric_limits<uint32_t>::max()
             : static_cast<uint32_t>(percentX1000);
}

inline uint64_t ComputeDWTResidualCycles(uint64_t totalCycles,
                                         uint64_t measuredCycles) {
  return totalCycles > measuredCycles ? totalCycles - measuredCycles : 0;
}

inline bool ShouldColorProfilerValueRed(uint32_t absolutePctX1000,
                                        uint32_t budgetPctX1000) {
  return budgetPctX1000 > 0 && absolutePctX1000 > budgetPctX1000;
}

inline bool ShouldColorProfilerValueGreen(uint32_t absolutePctX1000,
                                          uint32_t budgetPctX1000) {
  return budgetPctX1000 > 0 && absolutePctX1000 <= budgetPctX1000;
}

inline const char *GetProfilerUseColorStart(uint32_t absolutePctX1000,
                                            uint32_t budgetPctX1000) {
  if (ShouldColorProfilerValueRed(absolutePctX1000, budgetPctX1000)) {
    return "\x1b[31m";
  }
  if (ShouldColorProfilerValueGreen(absolutePctX1000, budgetPctX1000)) {
    return "\x1b[32m";
  }
  return "";
}

inline const char *GetProfilerUseColorEnd(uint32_t absolutePctX1000,
                                          uint32_t budgetPctX1000) {
  return GetProfilerUseColorStart(absolutePctX1000, budgetPctX1000)[0] != '\0'
             ? "\x1b[0m"
             : "";
}

#if !defined(HOST_TEST) && !defined(WASM) && ENABLE_PROFILING
namespace DWTProfiler {
constexpr uint32_t kReportIntervalUs = 5000000;

inline DWTProfileStats *&statsHead() {
  static DWTProfileStats *head = nullptr;
  return head;
}

inline DWTProfileStats *&statsTail() {
  static DWTProfileStats *tail = nullptr;
  return tail;
}

inline uint32_t &reportStartMicros() {
  static uint32_t start = 0;
  return start;
}

inline bool &flushInProgress() {
  static bool active = false;
  return active;
}

inline bool &flushPending() {
  static bool pending = false;
  return pending;
}

inline uint32_t &activeScopeCount() {
  static uint32_t count = 0;
  return count;
}

inline DWTProfileMatrix *&profileMatrix() {
  static DWTProfileMatrix *matrix = nullptr;
  return matrix;
}

struct Snapshot {
  uint32_t cycles_;
};

inline bool ensureReady() {
  static bool initialized = false;
  static bool available = false;

  if (initialized) {
    return available;
  }

  initialized = true;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55;

  if (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) {
    return false;
  }

  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  available = true;
  return true;
}

inline Snapshot snapshot() { return {.cycles_ = DWT->CYCCNT}; }

inline uint32_t diff32(uint32_t newer, uint32_t older) { return newer - older; }

inline void registerStats(DWTProfileStats &stats) {
  if (stats.registered_) {
    return;
  }

  stats.registered_ = true;
  stats.next_ = nullptr;

  if (!statsHead()) {
    statsHead() = &stats;
    statsTail() = &stats;
    return;
  }

  statsTail()->next_ = &stats;
  statsTail() = &stats;
}

inline void resetStats(DWTProfileStats &stats) { stats.totalCycles_ = 0; }

inline uint64_t resolveCyclesForDisplay(const DWTProfileStats &stats) {
  if (stats.aggregateChildCount_ == 0) {
    return stats.totalCycles_;
  }

  uint64_t cycles = 0;
  for (uint8_t i = 0; i < stats.aggregateChildCount_; ++i) {
    const DWTProfileStats *child = stats.aggregateChildren_[i];
    if (child) {
      cycles += resolveCyclesForDisplay(*child);
    }
  }
  return cycles;
}

inline void formatProfileLabel(char *buffer, size_t bufferSize,
                               const char *name, uint32_t budgetPctX1000) {
  if (budgetPctX1000 == 0) {
    npf_snprintf(buffer, bufferSize, "%s", name ? name : "unnamed");
    return;
  }

  if (budgetPctX1000 < 1000U && (budgetPctX1000 % 10U) != 0) {
    npf_snprintf(buffer, bufferSize, "%s(0.%03lu%%)", name ? name : "unnamed",
                 static_cast<unsigned long>(budgetPctX1000));
    return;
  }

  const uint32_t budgetPctX100 = (budgetPctX1000 + 5U) / 10U;
  const uint32_t whole = budgetPctX100 / 100U;
  const uint32_t fraction = budgetPctX100 % 100U;
  if (fraction == 0) {
    npf_snprintf(buffer, bufferSize, "%s(%lu%%)", name ? name : "unnamed",
                 static_cast<unsigned long>(whole));
  } else if ((fraction % 10U) == 0) {
    npf_snprintf(buffer, bufferSize, "%s(%lu.%01lu%%)", name ? name : "unnamed",
                 static_cast<unsigned long>(whole),
                 static_cast<unsigned long>(fraction / 10U));
  } else {
    npf_snprintf(buffer, bufferSize, "%s(%lu.%02lu%%)", name ? name : "unnamed",
                 static_cast<unsigned long>(whole),
                 static_cast<unsigned long>(fraction));
  }
}

inline void printPercentValue(uint64_t cycles, uint32_t elapsedUs,
                              uint32_t budgetPctX1000) {
  const uint32_t cpuHz = SystemCoreClock > 0 ? SystemCoreClock : 1U;
  const uint32_t percentX1000 =
      ComputeDWTAbsolutePercentX1000(cycles, cpuHz, elapsedUs);
  const uint32_t percentX100 = (percentX1000 + 5U) / 10U;
  const char *colorStart =
      GetProfilerUseColorStart(percentX1000, budgetPctX1000);
  const char *colorEnd = GetProfilerUseColorEnd(percentX1000, budgetPctX1000);
  npf_pprintf(&Trace::trace_uart_putc, nullptr, " %s%5lu.%02lu%%%s", colorStart,
              static_cast<unsigned long>(percentX100 / 100U),
              static_cast<unsigned long>(percentX100 % 100U), colorEnd);
}

inline void printProfileMatrix(const DWTProfileMatrix &matrix,
                               uint32_t elapsedUs) {
  if (!matrix.rows_ || matrix.columnCount_ == 0 ||
      matrix.columnCount_ > SONG_CHANNEL_COUNT) {
    return;
  }

  npf_pprintf(&Trace::trace_uart_putc, nullptr, "[DWT] %-22s", "absolute CPU");
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "     Total");
  for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
    npf_pprintf(&Trace::trace_uart_putc, nullptr, "        V%lu",
                static_cast<unsigned long>(column + 1U));
  }
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "\r\n");

  uint64_t measuredByChannel[SONG_CHANNEL_COUNT] = {};
  for (uint32_t rowIndex = 0; rowIndex < matrix.rowCount_; ++rowIndex) {
    const DWTProfileMatrixRow &row = matrix.rows_[rowIndex];
    char label[32];
    formatProfileLabel(label, sizeof(label), row.name_, row.budgetPctX1000_);
    npf_pprintf(&Trace::trace_uart_putc, nullptr, "[DWT] %-22s", label);

    uint64_t rowCycles = 0;
    for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
      const uint64_t cycles =
          row.columns_[column] ? row.columns_[column]->totalCycles_ : 0;
      measuredByChannel[column] += cycles;
      rowCycles += cycles;
    }
    printPercentValue(rowCycles, elapsedUs,
                      row.budgetPctX1000_ * matrix.columnCount_);
    for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
      const uint64_t cycles =
          row.columns_[column] ? row.columns_[column]->totalCycles_ : 0;
      printPercentValue(cycles, elapsedUs, row.budgetPctX1000_);
    }
    npf_pprintf(&Trace::trace_uart_putc, nullptr, "\r\n");
  }

  uint64_t channelTotalCycles = 0;
  uint64_t otherCycles = 0;
  uint64_t otherByChannel[SONG_CHANNEL_COUNT] = {};
  for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
    const uint64_t totalCycles =
        matrix.channelTotals_[column]
            ? matrix.channelTotals_[column]->totalCycles_
            : 0;
    channelTotalCycles += totalCycles;
    otherByChannel[column] =
        ComputeDWTResidualCycles(totalCycles, measuredByChannel[column]);
    otherCycles += otherByChannel[column];
  }

  npf_pprintf(&Trace::trace_uart_putc, nullptr, "[DWT] %-22s", "Other");
  printPercentValue(otherCycles, elapsedUs, 0);
  for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
    printPercentValue(otherByChannel[column], elapsedUs, 0);
  }
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "\r\n");

  char label[32];
  formatProfileLabel(label, sizeof(label), "Channel total",
                     matrix.channelBudgetPctX1000_);
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "[DWT] %-22s", label);
  printPercentValue(channelTotalCycles, elapsedUs,
                    matrix.channelBudgetPctX1000_ * matrix.columnCount_);
  for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
    const uint64_t cycles = matrix.channelTotals_[column]
                                ? matrix.channelTotals_[column]->totalCycles_
                                : 0;
    printPercentValue(cycles, elapsedUs, matrix.channelBudgetPctX1000_);
  }
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "\r\n");

  const uint64_t voicesTotalCycles =
      matrix.voicesTotal_ ? matrix.voicesTotal_->totalCycles_ : 0;
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "[DWT] %-22s", "Voices other");
  printPercentValue(
      ComputeDWTResidualCycles(voicesTotalCycles, channelTotalCycles),
      elapsedUs, 0);
  for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
    npf_pprintf(&Trace::trace_uart_putc, nullptr, "          ");
  }
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "\r\n");

  formatProfileLabel(label, sizeof(label), "Voices total",
                     matrix.voicesBudgetPctX1000_);
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "[DWT] %-22s", label);
  printPercentValue(voicesTotalCycles, elapsedUs, matrix.voicesBudgetPctX1000_);
  for (uint32_t column = 0; column < matrix.columnCount_; ++column) {
    npf_pprintf(&Trace::trace_uart_putc, nullptr, "          ");
  }
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "\r\n");
}

inline void printStatsLine(const DWTProfileStats &stats, uint32_t elapsedUs) {
  char label[32];
  formatProfileLabel(label, sizeof(label), stats.name_, stats.budgetPctX1000_);
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "[DWT] %-22s", label);
  printPercentValue(resolveCyclesForDisplay(stats), elapsedUs,
                    stats.budgetPctX1000_);
  npf_pprintf(&Trace::trace_uart_putc, nullptr, "\r\n");
}

inline void accumulateSample(DWTProfileStats &stats, uint32_t cycles) {
  stats.totalCycles_ += cycles;
}

inline void collectOrderedStats(DWTProfileStats **statsArray, size_t &count,
                                size_t capacity) {
  count = 0;
  for (DWTProfileStats *stats = statsHead(); stats; stats = stats->next_) {
    if (!stats->printIndividually_) {
      continue;
    }
    if (count >= capacity) {
      break;
    }
    statsArray[count++] = stats;
  }
}

inline bool shouldSortAfter(const DWTProfileStats &lhs,
                            const DWTProfileStats &rhs) {
  if (lhs.printLast_ != rhs.printLast_) {
    return lhs.printLast_ && !rhs.printLast_;
  }
  return lhs.displayOrder_ > rhs.displayOrder_;
}

inline void flushAll(uint32_t now) {
  if (flushInProgress()) {
    return;
  }

  const uint32_t elapsedUs = now - reportStartMicros();

  constexpr size_t kMaxRegisteredStats = 64;
  DWTProfileStats *orderedStats[kMaxRegisteredStats];
  size_t orderedCount = 0;
  collectOrderedStats(orderedStats, orderedCount, kMaxRegisteredStats);
  for (size_t i = 1; i < orderedCount; ++i) {
    DWTProfileStats *key = orderedStats[i];
    size_t j = i;
    while (j > 0 && shouldSortAfter(*orderedStats[j - 1], *key)) {
      orderedStats[j] = orderedStats[j - 1];
      --j;
    }
    orderedStats[j] = key;
  }

  flushInProgress() = true;
  npf_pprintf(&Trace::trace_uart_putc, nullptr,
              "[DWT] ----------------------------------------\r\n");
  if (profileMatrix()) {
    printProfileMatrix(*profileMatrix(), elapsedUs);
  }
  for (size_t i = 0; i < orderedCount; ++i) {
    printStatsLine(*orderedStats[i], elapsedUs);
  }

  for (DWTProfileStats *stats = statsHead(); stats; stats = stats->next_) {
    resetStats(*stats);
  }

  reportStartMicros() = micros();
  flushPending() = false;
  flushInProgress() = false;
}

inline void enterScope() { activeScopeCount()++; }

inline void leaveScope(bool flushBoundary) {
  if (activeScopeCount() == 0) {
    return;
  }

  activeScopeCount()--;
  if (activeScopeCount() == 0 && flushPending() && flushBoundary) {
    flushAll(micros());
  }
}

inline void record(DWTProfileStats &stats, const Snapshot &start,
                   const Snapshot &end) {
  registerStats(stats);

  const uint32_t cycles = diff32(end.cycles_, start.cycles_);
  const uint32_t now = micros();

  if (reportStartMicros() == 0) {
    reportStartMicros() = now;
  }

  accumulateSample(stats, cycles);

  if ((now - reportStartMicros()) < kReportIntervalUs) {
    return;
  }

  flushPending() = true;
}

inline void recordTotals(DWTProfileStats &stats, uint32_t cycles) {
  registerStats(stats);

  const uint32_t now = micros();

  if (reportStartMicros() == 0) {
    reportStartMicros() = now;
  }

  accumulateSample(stats, cycles);

  if ((now - reportStartMicros()) < kReportIntervalUs) {
    return;
  }

  flushPending() = true;
}
} // namespace DWTProfiler

class ScopedDWTProfile {
public:
  explicit ScopedDWTProfile(DWTProfileStats *stats,
                            DWTProfileStats *secondaryStats = nullptr)
      : stats_(stats), active_(false), secondaryStats_(secondaryStats) {
    if (!stats_ || !DWTProfiler::ensureReady()) {
      return;
    }

    DWTProfiler::enterScope();
    start_ = DWTProfiler::snapshot();
    active_ = true;
  }

  ~ScopedDWTProfile() {
    if (!active_) {
      return;
    }

    const DWTProfiler::Snapshot end = DWTProfiler::snapshot();
    DWTProfiler::record(*stats_, start_, end);
    if (secondaryStats_) {
      DWTProfiler::recordTotals(
          *secondaryStats_, DWTProfiler::diff32(end.cycles_, start_.cycles_));
    }
    DWTProfiler::leaveScope(stats_->flushBoundary_);
  }

private:
  DWTProfileStats *stats_;
  bool active_;
  DWTProfileStats *secondaryStats_;
  DWTProfiler::Snapshot start_{};
};

class AccumulatingDWTProfile {
public:
  explicit AccumulatingDWTProfile(DWTProfileStats *stats)
      : stats_(stats), active_(false), segmentActive_(false),
        hasSamples_(false), cycles_(0) {
    if (!stats_ || !DWTProfiler::ensureReady()) {
      return;
    }

    DWTProfiler::enterScope();
    active_ = true;
  }

  void Begin() {
    if (!active_ || segmentActive_) {
      return;
    }

    segmentStart_ = DWTProfiler::snapshot();
    segmentActive_ = true;
  }

  void End() {
    if (!active_ || !segmentActive_) {
      return;
    }

    const DWTProfiler::Snapshot end = DWTProfiler::snapshot();
    cycles_ += DWTProfiler::diff32(end.cycles_, segmentStart_.cycles_);
    segmentActive_ = false;
    hasSamples_ = true;
  }

  ~AccumulatingDWTProfile() {
    if (!active_) {
      return;
    }

    if (segmentActive_) {
      End();
    }
    if (hasSamples_) {
      DWTProfiler::recordTotals(*stats_, cycles_);
    }
    DWTProfiler::leaveScope(false);
  }

private:
  DWTProfileStats *stats_;
  bool active_;
  bool segmentActive_;
  bool hasSamples_;
  uint32_t cycles_;
  DWTProfiler::Snapshot segmentStart_{};
};

inline void RegisterDWTProfileStats(DWTProfileStats *stats) {
  if (!stats || !DWTProfiler::ensureReady()) {
    return;
  }

  DWTProfiler::registerStats(*stats);
}

inline void RegisterDWTProfileMatrix(DWTProfileMatrix *matrix) {
  DWTProfiler::profileMatrix() = matrix;
}
#else
class ScopedDWTProfile {
public:
  explicit ScopedDWTProfile(DWTProfileStats *stats,
                            DWTProfileStats *secondaryStats = nullptr) {
    (void)stats;
    (void)secondaryStats;
  }
};

class AccumulatingDWTProfile {
public:
  explicit AccumulatingDWTProfile(DWTProfileStats *stats) { (void)stats; }

  void Begin() {}
  void End() {}
};

inline void RegisterDWTProfileStats(DWTProfileStats *stats) { (void)stats; }

inline void RegisterDWTProfileMatrix(DWTProfileMatrix *matrix) { (void)matrix; }
#endif

#endif
