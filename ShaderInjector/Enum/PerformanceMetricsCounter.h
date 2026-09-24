#pragma once

#include <cstdint>

namespace PerformanceMetrics
{
	enum class Counter : uint8_t
	{
		Present,
		DrawInstanced,
		DrawIndexedInstanced,
		Dispatch,
		ExecuteIndirect,
		ExecutionBoundaryCandidate,
		ExecutionBoundaryRecorded,
		DescriptorCopy,
		DescriptorCopySimple,
		DescriptorCopyRegistryHit,
		MipPassAttempted,
		MipPassSucceeded,
		MipPassFailed,
		MipLevelDraw,
		MipTexels,
		MipResourceCreated,
		MipResourceReused,
		CustomPassAttempted,
		CustomPassSucceeded,
		ReplacementPassAttempted,
		ReplacementPassSucceeded,
		ShaderResourceBindAttempted,
		ShaderResourceBindSucceeded,
		Count,
	};
} //namespace PerformanceMetrics
