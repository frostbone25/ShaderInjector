#pragma once

//custom
#include "HookD3D12.h"

namespace HookD3D12
{
	void CapturePipelineStateStream(const D3D12_PIPELINE_STATE_STREAM_DESC* pipelineStreamDescription, ID3D12PipelineState* pipelineState);
}
