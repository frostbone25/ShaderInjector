#pragma once

#include <cstdint>

namespace PerformanceMetrics
{
	//the report combines the counters from every thread into plain values.
	struct TimingSnapshot
	{
		uint64_t invocations = 0;
		uint64_t samples = 0;
		uint64_t totalTicks = 0;
		uint64_t maximumTicks = 0;
	};
} //namespace PerformanceMetrics
