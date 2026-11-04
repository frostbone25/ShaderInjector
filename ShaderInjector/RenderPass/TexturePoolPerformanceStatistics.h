#pragma once

#include <cstddef>
#include <cstdint>

namespace RenderPassTexturePool
{
	struct PerformanceStatistics
	{
		size_t liveTextures = 0;
		uint64_t liveBytes = 0;
		size_t retiredTextures = 0;
		uint64_t retiredBytes = 0;
	};
}
