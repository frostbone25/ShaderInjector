#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"

namespace HookD3D12
{
	HRESULT STDMETHODCALLTYPE Hook_CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_CreateGraphicsPipelineState(device, description, interfaceId, pipelineState);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		if (IsInsideRenderPassInjection())
			return Original_CreateGraphicsPipelineState(device, description, interfaceId, pipelineState);

		ScopedPipelineActivity pipelineActivity(!gInsideOverlayResourceCreation);
		HRESULT result = Original_CreateGraphicsPipelineState(device, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && 
			!gInsideOverlayResourceCreation && 
			description && 
			pipelineState && 
			*pipelineState)
			CaptureGraphicsPipelineState(description, static_cast<ID3D12PipelineState*>(*pipelineState));

		return result;
	}
}
