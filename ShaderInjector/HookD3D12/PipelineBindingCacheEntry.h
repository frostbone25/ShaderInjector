#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HookD3D12
{
	struct PipelineBindingCacheEntry
	{
		ID3D12PipelineState* requestedPipelineState = nullptr;
		ID3D12PipelineState* resolvedPipelineState = nullptr;
		uint64_t overrideGeneration = 0;
	};
} //namespace HookD3D12
