#pragma once

#include <cstddef>
#include <cstdint>

namespace ShaderResourceRuntime
{
	struct PerformanceStatistics
	{
		size_t commandLists = 0;
		size_t executionSlots = 0;
		size_t blockedSlots = 0;
		size_t heapPages = 0;
		uint64_t descriptorCapacity = 0;
	};
}
