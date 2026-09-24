#include "../HookD3D12.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_LoadPipeline(library, name, description, interfaceId, pipelineState);
	}

	HRESULT __stdcall Handle_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT result = Original_LoadPipeline(library, name, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && description && pipelineState && *pipelineState)
			CapturePipelineStateStream(description, static_cast<ID3D12PipelineState*>(*pipelineState));

		return result;
	}
} //namespace HookD3D12
