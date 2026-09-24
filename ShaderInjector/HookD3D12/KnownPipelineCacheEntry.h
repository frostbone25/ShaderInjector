#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HookD3D12
{
	struct KnownPipelineCacheEntry
	{
		ID3D12PipelineState* pipelineState = nullptr;
		uint64_t overrideGeneration = 0;
		bool isKnown = false;
	};
} //namespace HookD3D12
