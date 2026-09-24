#include "../HookD3D12.h"

#include <string>

#include "GUI/ShaderInjectorGUI.h"
#include "StringHelper.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_CreatePipelineLibrary(ID3D12Device1* device, const void* blob, SIZE_T blobSize, REFIID interfaceId, void** pipelineLibrary)
	{
		return Handle_CreatePipelineLibrary(device, blob, blobSize, interfaceId, pipelineLibrary);
	}

	HRESULT __stdcall Handle_CreatePipelineLibrary(ID3D12Device1* device, const void* libraryBlob, SIZE_T blobSize, REFIID interfaceId, void** pipelineLibraryObject)
	{
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: CreatePipelineLibrary Blob Size: " + std::to_string(blobSize));

		HRESULT result = Original_CreatePipelineLibrary(device, libraryBlob, blobSize, interfaceId, pipelineLibraryObject);

		const bool incompatibleSerializedLibrary =
			result == D3D12_ERROR_DRIVER_VERSION_MISMATCH ||
			result == D3D12_ERROR_ADAPTER_NOT_FOUND;

		if (incompatibleSerializedLibrary && libraryBlob && blobSize > 0 && pipelineLibraryObject)
		{
			ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: serialized pipeline library rejected hr = " + StringHelper::FormatHRESULT(result) + "; retrying with an empty library");

			*pipelineLibraryObject = nullptr;
			result = Original_CreatePipelineLibrary(device, nullptr, 0, interfaceId, pipelineLibraryObject);

			if (SUCCEEDED(result))
			{
				ShaderInjectorGUI::WriteToRuntimeLogSuccess("HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: empty pipeline library created; cached PSOs will rebuild");
			}
			else
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: empty pipeline library retry failed hr = " + StringHelper::FormatHRESULT(result));
			}
		}

		if (SUCCEEDED(result) && pipelineLibraryObject && *pipelineLibraryObject)
			HookPipelineLibrary(static_cast<ID3D12PipelineLibrary*>(*pipelineLibraryObject));

		return result;
	}
} //namespace HookD3D12
