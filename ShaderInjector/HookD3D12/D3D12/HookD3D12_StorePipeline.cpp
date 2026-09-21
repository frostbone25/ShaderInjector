#include "HookD3D12HookHandlers.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_StorePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, ID3D12PipelineState* pipelineState)
	{
		return Handle_StorePipeline(library, name, pipelineState);
	}

	HRESULT __stdcall Handle_StorePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, ID3D12PipelineState* pipelineState)
	{
		return Original_StorePipeline(library, name, pipelineState);
	}
}
