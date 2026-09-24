#include "../HookD3D12.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_LoadComputePipeline(library, name, description, interfaceId, pipelineState);
	}

	HRESULT __stdcall Handle_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT result = Original_LoadComputePipeline(library, name, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && description && pipelineState && *pipelineState)
			CaptureComputePipelineState(description, static_cast<ID3D12PipelineState*>(*pipelineState), false);

		return result;
	}
} //namespace HookD3D12
