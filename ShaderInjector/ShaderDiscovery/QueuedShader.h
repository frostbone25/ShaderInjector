#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Enum/ShaderAutomaticDiscoveryPipelineSource.h"
#include "ShaderTarget/ShaderIdentityKey.h"

namespace ShaderAutomaticDiscovery
{
	//retain the captured bytecode and its pipeline until a worker can analyze it.
	struct QueuedShader
	{
		ShaderTarget::ShaderIdentityKey shaderKey;
		PipelineSource source = PipelineSource::Stream;
		int pipelineIndex = -1;
		double priority = 0.0;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
		std::vector<uint8_t> shaderBytecode;
	};
} //namespace ShaderAutomaticDiscovery
