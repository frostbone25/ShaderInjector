#pragma once

//custom
#include "HookD3D12/HookD3D12.h"

namespace HookD3D12
{
	void CaptureGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState);
	void CaptureComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState, bool shouldRegisterAsKnownPipeline);
}
