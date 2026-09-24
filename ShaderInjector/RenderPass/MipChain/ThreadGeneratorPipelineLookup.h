#pragma once

#include <d3d12.h>

#include "RenderPass/MipChain/DeviceResources.h"
#include "RenderPass/RenderPass.h"

namespace RenderPassMipChain
{
	//reuse the generator pipeline for one pass, format, and graphics or compute mode.
	struct ThreadGeneratorPipelineLookup
	{
		DeviceResources* deviceResources = nullptr;
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		bool computePipeline = false;
		ID3D12PipelineState* pipelineState = nullptr;
	};
} //namespace RenderPassMipChain
