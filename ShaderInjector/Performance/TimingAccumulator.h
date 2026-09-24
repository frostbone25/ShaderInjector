#pragma once

#include <atomic>
#include <cstdint>

namespace PerformanceMetrics
{
	//each thread records timing samples here until the next report collects them.
	struct TimingAccumulator
	{
		std::atomic<uint64_t> invocations = 0;
		std::atomic<uint64_t> samples = 0;
		std::atomic<uint64_t> totalTicks = 0;
		std::atomic<uint64_t> maximumTicks = 0;
	};
} //namespace PerformanceMetrics
