#include "HookD3D12HookHandlers.h"

#include <string>

#include "GUI/ShaderInjectorGUI.h"
#include "StringHelper.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_LoadGraphicsPipeline(library, name, description, interfaceId, pipelineState);
	}

	HRESULT __stdcall Hook_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_LoadComputePipeline(library, name, description, interfaceId, pipelineState);
	}

	HRESULT __stdcall Hook_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_LoadPipeline(library, name, description, interfaceId, pipelineState);
	}

	HRESULT __stdcall Hook_StorePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, ID3D12PipelineState* pipelineState)
	{
		return Handle_StorePipeline(library, name, pipelineState);
	}

	SIZE_T __stdcall Hook_GetSerializedSize(ID3D12PipelineLibrary* library)
	{
		return Handle_GetSerializedSize(library);
	}

	HRESULT __stdcall Hook_Serialize(ID3D12PipelineLibrary* library, void* data, SIZE_T dataSize)
	{
		return Handle_Serialize(library, data, dataSize);
	}

	HRESULT __stdcall Hook_CreatePipelineLibrary(ID3D12Device1* device, const void* blob, SIZE_T blobSize, REFIID interfaceId, void** pipelineLibrary)
	{
		return Handle_CreatePipelineLibrary(device, blob, blobSize, interfaceId, pipelineLibrary);
	}

	HRESULT __stdcall Handle_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT result = Original_LoadGraphicsPipeline(library, name, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && description && pipelineState && *pipelineState)
			CaptureGraphicsPipelineState(description, static_cast<ID3D12PipelineState*>(*pipelineState));

		return result;
	}

	HRESULT __stdcall Handle_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT result = Original_LoadComputePipeline(library, name, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && description && pipelineState && *pipelineState)
			CaptureComputePipelineState(description, static_cast<ID3D12PipelineState*>(*pipelineState), false);

		return result;
	}

	HRESULT __stdcall Handle_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT result = Original_LoadPipeline(library, name, description, interfaceId, pipelineState);

		if (SUCCEEDED(result) && description && pipelineState && *pipelineState)
			CapturePipelineStateStream(description, static_cast<ID3D12PipelineState*>(*pipelineState));

		return result;
	}

	HRESULT __stdcall Handle_StorePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, ID3D12PipelineState* pipelineState)
	{
		return Original_StorePipeline(library, name, pipelineState);
	}

	SIZE_T __stdcall Handle_GetSerializedSize(ID3D12PipelineLibrary* library)
	{
		return Original_GetSerializedSize(library);
	}

	HRESULT __stdcall Handle_Serialize(ID3D12PipelineLibrary* library, void* data, SIZE_T dataSize)
	{
		return Original_Serialize(library, data, dataSize);
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
			ShaderInjectorGUI::WriteToRuntimeLogWarning(
				"HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: serialized pipeline library rejected hr=" +
				StringHelper::FormatHRESULT(result) + "; retrying with an empty library");

			*pipelineLibraryObject = nullptr;
			result = Original_CreatePipelineLibrary(device, nullptr, 0, interfaceId, pipelineLibraryObject);

			if (SUCCEEDED(result))
			{
				ShaderInjectorGUI::WriteToRuntimeLogSuccess(
					"HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: empty pipeline library created; cached PSOs will rebuild");
			}
			else
			{
				ShaderInjectorGUI::WriteToRuntimeLogError(
					"HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: empty pipeline library retry failed hr=" +
					StringHelper::FormatHRESULT(result));
			}
		}

		if (SUCCEEDED(result) && pipelineLibraryObject && *pipelineLibraryObject)
			HookPipelineLibrary(static_cast<ID3D12PipelineLibrary*>(*pipelineLibraryObject));

		return result;
	}
}
