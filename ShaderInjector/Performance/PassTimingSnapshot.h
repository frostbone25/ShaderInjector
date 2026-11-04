#pragma once

#include <string>

#include "Performance/GPUTimingStatistics.h"

namespace RenderPassGPUProfiler
{
	struct PassTimingSnapshot
	{
		std::string passName;
		GPUTimingStatistics statistics;
	};
}
