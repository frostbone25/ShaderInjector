#pragma once

#include <d3d12.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderTargetState.h"

namespace RenderPassExecutor
{
	//cache a pipeline lookup for one thread and its current render-target layout.
	struct ThreadPipelineLookup
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		ID3D12RootSignature* rootSignature = nullptr;
		RenderTargetState renderTargets;
		ID3D12PipelineState* pipelineState = nullptr;
	};
} //namespace RenderPassExecutor
