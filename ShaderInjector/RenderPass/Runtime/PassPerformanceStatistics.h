#pragma once

#include <cstdint>
#include <string>

namespace RenderPassRuntime
{
	struct PassPerformanceStatistics
	{
		std::string name;
		std::string type;
		bool enabled = false;
		uint64_t triggers = 0;
		uint64_t successes = 0;
		uint64_t failures = 0;
		uint64_t blockedInputs = 0;
	};
}
