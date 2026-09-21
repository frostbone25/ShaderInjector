#include "../HookD3D12.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_Serialize(ID3D12PipelineLibrary* library, void* data, SIZE_T dataSize)
	{
		return Handle_Serialize(library, data, dataSize);
	}

	HRESULT __stdcall Handle_Serialize(ID3D12PipelineLibrary* library, void* data, SIZE_T dataSize)
	{
		return Original_Serialize(library, data, dataSize);
	}
}
