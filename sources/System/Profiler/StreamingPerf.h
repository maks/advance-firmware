#pragma once
#include <stdint.h>
#if defined(ADV)
#include "stm32h7xx.h"
// Each metric/counter has one firmware writer. Halt for coherent snapshots.
typedef struct {
  uint32_t count;
  uint32_t lastCycles;
  uint32_t maxCycles;
  uint64_t totalCycles;
  uint32_t epoch;
  uint32_t windowMaxCycles;
} StreamingPerfMetric;
typedef struct {
  uint32_t version;
  uint32_t cpuHz;
  uint32_t captureEpoch;
  uint32_t audioUnderruns;
  uint32_t pageMisses;
  uint32_t interpolationMisses;
  uint32_t queueFull;
  uint32_t queueHighWater;
  uint32_t streamErrors;
  uint32_t fileCacheHits;
  uint32_t sdReadTransfers;
  uint32_t sdReadSectors;
  uint32_t sdBounceTransfers;
  uint32_t sdErrors;
  uint32_t sdTimeouts;
  StreamingPerfMetric audio;
  StreamingPerfMetric streamRead;
  StreamingPerfMetric streamQueueWait;
  StreamingPerfMetric streamOpen;
  StreamingPerfMetric streamSeek;
  StreamingPerfMetric streamDataRead;
  StreamingPerfMetric sdWait;
  StreamingPerfMetric uiDispatch;
  StreamingPerfMetric uiGap;
  StreamingPerfMetric inputScanGap;
  StreamingPerfMetric inputQueueWait;
  StreamingPerfMetric inputDispatchLatency;
} StreamingPerfCounters;
#ifdef __cplusplus
extern "C" {
#endif
extern volatile StreamingPerfCounters streamingPerf;
static inline void RecordStreamingPerf(volatile StreamingPerfMetric *metric,
                                       uint32_t cycles) {
  if (metric->epoch != streamingPerf.captureEpoch) {
    metric->epoch = streamingPerf.captureEpoch;
    metric->windowMaxCycles = 0;
  }
  metric->lastCycles = cycles;
  metric->maxCycles = cycles > metric->maxCycles ? cycles : metric->maxCycles;
  metric->windowMaxCycles =
      cycles > metric->windowMaxCycles ? cycles : metric->windowMaxCycles;
  metric->totalCycles = metric->totalCycles + cycles;
  metric->count = metric->count + 1;
}
#ifdef __cplusplus
}
class StreamingPerfScope {
public:
  explicit StreamingPerfScope(volatile StreamingPerfMetric &metric)
      : metric_(metric), start_(DWT->CYCCNT) {}
  ~StreamingPerfScope() { Record(metric_, DWT->CYCCNT - start_); }
  static void Record(volatile StreamingPerfMetric &metric, uint32_t cycles) {
    RecordStreamingPerf(&metric, cycles);
  }

private:
  volatile StreamingPerfMetric &metric_;
  uint32_t start_;
};
#endif
#endif
