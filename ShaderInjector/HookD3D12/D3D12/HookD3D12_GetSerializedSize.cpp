#include "HookD3D12HookHandlers.h"

namespace HookD3D12
{
	SIZE_T __stdcall Hook_GetSerializedSize(ID3D12PipelineLibrary* library)
	{
		return Handle_GetSerializedSize(library);
	}

	SIZE_T __stdcall Handle_GetSerializedSize(ID3D12PipelineLibrary* library)
	{
		return Original_GetSerializedSize(library);
	}
}
