#pragma once

#include <cstdint>

namespace ShaderAutomaticDiscovery
{
	//report captured shader work without reading package or database state.
	struct ShaderTargetProgress
	{
		uint64_t total = 0;
		uint64_t completed = 0;
		uint64_t created = 0;
		uint64_t failed = 0;
		uint64_t queued = 0;
		uint64_t running = 0;
	};
} //namespace ShaderAutomaticDiscovery
