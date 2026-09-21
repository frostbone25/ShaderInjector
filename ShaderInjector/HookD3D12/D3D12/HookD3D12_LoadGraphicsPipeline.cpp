#include "../HookD3D12.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_LoadGraphicsPipeline(library, name, description, interfaceId, pipelineState);
	}

	HRESULT __stdcall Handle_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT result = Original_LoadGraphicsPipeline(library, name, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && description && pipelineState && *pipelineState)
			CaptureGraphicsPipelineState(description, static_cast<ID3D12PipelineState*>(*pipelineState));

		return result;
	}
}
