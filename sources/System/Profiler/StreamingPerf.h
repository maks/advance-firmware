#pragma once
#include <cstdint>
#if defined(ADV)
#include "stm32h7xx.h"
// Single writer per metric. Halt the core for coherent debugger snapshots.
struct StreamingPerfMetric {
  uint32_t count;
  uint32_t lastCycles;
  uint32_t maxCycles;
  uint64_t totalCycles;
};
struct StreamingPerfCounters {
  uint32_t version;
  uint32_t cpuHz;
  uint32_t audioUnderruns;
  StreamingPerfMetric audio;
  StreamingPerfMetric streamRead;
  StreamingPerfMetric uiDispatch;
  StreamingPerfMetric uiGap;
};
extern "C" volatile StreamingPerfCounters streamingPerf;
class StreamingPerfScope {
public:
  explicit StreamingPerfScope(volatile StreamingPerfMetric &metric)
      : metric_(metric), start_(DWT->CYCCNT) {}
  ~StreamingPerfScope() { Record(metric_, DWT->CYCCNT - start_); }
  static void Record(volatile StreamingPerfMetric &metric, uint32_t cycles) {
    metric.lastCycles = cycles;
    metric.maxCycles = cycles > metric.maxCycles ? cycles : metric.maxCycles;
    metric.totalCycles = metric.totalCycles + cycles;
    metric.count = metric.count + 1;
  }

private:
  volatile StreamingPerfMetric &metric_;
  uint32_t start_;
};
#endif
