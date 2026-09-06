//HookD3D12PipelineLibrary.cpp
#include "HookD3D12.h"

#include <string>

//3RD Party
#include "MinHook.h"

//custom
#include "ShaderInjectorGUI.h"
#include "StringHelper.h"
#include "VTableIndex.h"
#include "NativeVTableHooks.h"

namespace HookD3D12
{
	HRESULT __stdcall Hook_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT hr = Original_LoadGraphicsPipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CaptureGraphicsPipelineState(desc, (ID3D12PipelineState*)*ppPipelineState);
			//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_LoadGraphicsPipeline: PipelineLibrary LoadGraphicsPipeline captured cached PSO");
		}

		return hr;
	}

	HRESULT __stdcall Hook_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT hr = Original_LoadComputePipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CaptureComputePipelineState(desc, (ID3D12PipelineState*)*ppPipelineState, false);
			//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_LoadComputePipeline: PipelineLibrary LoadComputePipeline captured cached PSO");
		}

		return hr;
	}

	HRESULT __stdcall Hook_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT hr = Original_LoadPipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CapturePipelineStateStream(desc, (ID3D12PipelineState*)*ppPipelineState);
			//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_LoadPipeline: PipelineLibrary1 LoadPipeline captured cached stream PSO");
		}

		return hr;
	}

	HRESULT __stdcall Hook_StorePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, ID3D12PipelineState* pso)
	{
		return Original_StorePipeline(library, name, pso);
	}

	SIZE_T __stdcall Hook_GetSerializedSize(ID3D12PipelineLibrary* library)
	{
		return Original_GetSerializedSize(library);
	}

	HRESULT __stdcall Hook_Serialize(ID3D12PipelineLibrary* library, void* data, SIZE_T dataSize)
	{
		return Original_Serialize(library, data, dataSize);
	}

	void HookPipelineLibrary(ID3D12PipelineLibrary* pipelineLibrary)
	{
		if (!pipelineLibrary)
			return;

		using NativeVTableHooks::Family;
		bool installed = true;
		installed &= NativeVTableHooks::Install(pipelineLibrary, Family::PipelineLibrary, VTableIndex::indexStorePipeline, Hook_StorePipeline, &Original_StorePipeline, "StorePipeline");
		installed &= NativeVTableHooks::Install(pipelineLibrary, Family::PipelineLibrary, VTableIndex::indexLoadGraphicsPipeline, Hook_LoadGraphicsPipeline, &Original_LoadGraphicsPipeline, "LoadGraphicsPipeline");
		installed &= NativeVTableHooks::Install(pipelineLibrary, Family::PipelineLibrary, VTableIndex::indexLoadComputePipeline, Hook_LoadComputePipeline, &Original_LoadComputePipeline, "LoadComputePipeline");
		installed &= NativeVTableHooks::Install(pipelineLibrary, Family::PipelineLibrary, VTableIndex::indexGetSerializedSize, Hook_GetSerializedSize, &Original_GetSerializedSize, "GetSerializedSize");
		installed &= NativeVTableHooks::Install(pipelineLibrary, Family::PipelineLibrary, VTableIndex::indexSerialize, Hook_Serialize, &Original_Serialize, "Serialize");
		ID3D12PipelineLibrary1* pipelineLibrary1 = nullptr;
		if (SUCCEEDED(pipelineLibrary->QueryInterface(IID_PPV_ARGS(&pipelineLibrary1)))) {
			installed &= NativeVTableHooks::Install(pipelineLibrary1, Family::PipelineLibrary, VTableIndex::indexLoadPipeline, Hook_LoadPipeline, &Original_LoadPipeline, "LoadPipeline");
			pipelineLibrary1->Release();
		}
		ShaderInjectorGUI::WriteToRuntimeLog(installed
			? "HookD3D12PipelineLibrary->HookPipelineLibrary: Pipeline library hooks installed"
			: "HookD3D12PipelineLibrary->HookPipelineLibrary: Pipeline library hook installation incomplete");
	}

	HRESULT __stdcall Hook_CreatePipelineLibrary(ID3D12Device1* device, const void* pLibraryBlob, SIZE_T blobLength, REFIID riid, void** ppPipelineLibrary)
	{
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: CreatePipelineLibrary Blob Size: " + std::to_string(blobLength));

		HRESULT hr = Original_CreatePipelineLibrary(device, pLibraryBlob, blobLength, riid, ppPipelineLibrary);

		// Pipeline-library blobs are specific to the adapter and driver environment that serialized
		// them. RenderDoc's D3D12 capture layer can make an otherwise current game cache incompatible.
		// An empty library is the documented starting point for rebuilding cached pipeline entries.
		const bool incompatibleSerializedLibrary =
			hr == D3D12_ERROR_DRIVER_VERSION_MISMATCH ||
			hr == D3D12_ERROR_ADAPTER_NOT_FOUND;

		if (incompatibleSerializedLibrary && pLibraryBlob && blobLength > 0 && ppPipelineLibrary)
		{
			ShaderInjectorGUI::WriteToRuntimeLogWarning(
				"HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: serialized pipeline library rejected hr=" +
				StringHelper::FormatHRESULT(hr) + "; retrying with an empty library");

			*ppPipelineLibrary = nullptr;
			hr = Original_CreatePipelineLibrary(device, nullptr, 0, riid, ppPipelineLibrary);

			if (SUCCEEDED(hr))
			{
				ShaderInjectorGUI::WriteToRuntimeLogSuccess(
					"HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: empty pipeline library created; cached PSOs will rebuild");
			}
			else
			{
				ShaderInjectorGUI::WriteToRuntimeLogError(
					"HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: empty pipeline library retry failed hr=" +
					StringHelper::FormatHRESULT(hr));
			}
		}

		if (SUCCEEDED(hr) && ppPipelineLibrary && *ppPipelineLibrary)
		{
			auto* pipelineLibrary = (ID3D12PipelineLibrary*)(*ppPipelineLibrary);
			HookPipelineLibrary(pipelineLibrary);
		}

		return hr;
	}
}
