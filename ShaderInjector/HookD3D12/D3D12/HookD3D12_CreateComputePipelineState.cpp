#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"

namespace HookD3D12
{
	HRESULT STDMETHODCALLTYPE Hook_CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_CreateComputePipelineState(device, description, interfaceId, pipelineState);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;

		HRESULT result = Original_CreateComputePipelineState(device, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && 
			description && 
			pipelineState && 
			*pipelineState)
			CaptureComputePipelineState(description, static_cast<ID3D12PipelineState*>(*pipelineState), true);

		return result;
	}
}
