#pragma once

#include <atomic>
#include <cstdint>

namespace RenderPassRuntime
{
	//count attempts and results while the published configuration is active.
	struct RuntimeCounters
	{
		std::atomic<uint64_t> triggerCount = 0;
		std::atomic<uint64_t> executionCount = 0;
		std::atomic<uint64_t> executionFailureCount = 0;
		std::atomic<uint64_t> reportedTriggerCount = 0;
		std::atomic<uint64_t> reportedExecutionCount = 0;
		std::atomic<uint64_t> reportedFailureCount = 0;
		//zero means no attempt, one success, and two failure.
		std::atomic<uint8_t> lastExecutionResult = 0;
	};
} //namespace RenderPassRuntime
