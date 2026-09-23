#pragma once

#include <cstdint>
#include <d3d12.h>
#include <vector>

namespace HookD3D12
{
	struct ComputePipelineInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;

		uint64_t computeShaderHash = 0;

		SIZE_T computeShaderBytecodeSize = 0;

		std::vector<uint8_t> computeShaderBytecode;

		D3D12_COMPUTE_PIPELINE_STATE_DESC originalDescription = {};
	};
}
