#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Enum/PerformanceMetricsCounter.h"
#include "Enum/PerformanceMetricsTiming.h"
#include "Performance/TimingAccumulator.h"

namespace PerformanceMetrics
{
	//one record per thread avoids contention while hooks count their own work.
	struct ThreadMetrics
	{
		std::array<std::atomic<uint64_t>, static_cast<size_t>(Counter::Count)> counters{};
		std::array<TimingAccumulator, static_cast<size_t>(Timing::Count)> timings{};
	};
} //namespace PerformanceMetrics
