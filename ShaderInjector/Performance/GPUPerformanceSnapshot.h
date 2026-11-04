#pragma once

#include <cstdint>
#include <vector>

#include "Performance/PassTimingSnapshot.h"

namespace RenderPassGPUProfiler
{
	struct PerformanceSnapshot
	{
		bool available = false;
		uint64_t executionSampleInterval = 0;
		std::vector<PassTimingSnapshot> passes;
	};
}
