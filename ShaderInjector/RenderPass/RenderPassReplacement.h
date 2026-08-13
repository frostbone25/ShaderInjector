#pragma once

#include <string>

#include <d3d12.h>

#include "RenderPass/RenderPass.h"

namespace RenderPassReplacement
{
	ID3D12PipelineState* GetOrCreatePipeline(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12PipelineState* originalPipelineState,
		std::string& outError);
	void ReleaseResources();
}
