#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>

#include "RenderPass/RenderPass.h"

namespace RenderPassReplacement
{
	//reuse a replacement pipeline while the original state and shader blob are unchanged.
	struct ThreadPipelineLookup
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		ID3D12PipelineState* originalPipelineState = nullptr;
		uint64_t shaderBlobHash = 0;
		ID3D12PipelineState* replacementPipelineState = nullptr;
		std::string error;
	};
} //namespace RenderPassReplacement
