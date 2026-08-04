#pragma once

#include <d3d12.h>

namespace HookD3D12
{
	void RegisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject);
	void UnregisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject);
	bool IsKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject);
	bool MarkUntrackedBoundPipelineStateLocked(ID3D12PipelineState* pipelineStateObject);
}
