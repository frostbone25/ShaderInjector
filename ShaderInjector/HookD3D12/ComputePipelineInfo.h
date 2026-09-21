#pragma once

#include <cstdint>
#include <d3d12.h>
#include <vector>

namespace HookD3D12
{
	struct ComputePipelineInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;

		uint64_t csHash = 0;
		SIZE_T csSize = 0;
		std::vector<uint8_t> csBytecode;
		D3D12_COMPUTE_PIPELINE_STATE_DESC originalDesc = {};
	};
}
