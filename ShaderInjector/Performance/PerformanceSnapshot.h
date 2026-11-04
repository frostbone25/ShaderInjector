#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Enum/PerformanceMetricsCounter.h"
#include "Enum/PerformanceMetricsTiming.h"
#include "Performance/TimingSnapshot.h"

namespace PerformanceMetrics
{
	struct PerformanceSnapshot
	{
		bool available = false;
		double intervalMilliseconds = 0;
		double framesPerSecond = 0;
		uint64_t presents = 0;
		size_t metricThreadCount = 0;
		std::array<uint64_t, static_cast<size_t>(Counter::Count)> counters{};
		std::array<TimingSnapshot, static_cast<size_t>(Timing::Count)> timings{};
		uint64_t ticksPerSecond = 0;
	};
}
