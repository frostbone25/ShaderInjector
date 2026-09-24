#pragma once
#include "Enum/PerformanceMetricsCounter.h"
#include "Enum/PerformanceMetricsTiming.h"
#include "Performance/TimingAccumulator.h"

#include <cstdint>

namespace PerformanceMetrics
{
	void Increment(Counter counter, uint64_t amount = 1);

	//hot paths count every call, but only sampled calls read the clock twice.
	class ScopedTimer
	{
		TimingAccumulator* timingAccumulator = nullptr;
		uint64_t startTicks = 0;
		bool sampled = false;

	  public:
		explicit ScopedTimer(Timing timing, uint32_t sampleEvery = 1);
		~ScopedTimer();

		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;
	};

	//the present hook records a frame and writes a combined report every five seconds.
	bool RecordPresentAndMaybeLog();
} //namespace PerformanceMetrics
