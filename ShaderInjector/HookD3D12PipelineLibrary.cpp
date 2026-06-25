#include "HookD3D12.h"

#include <string>
#include <unordered_set>

#include "MinHook.h"
#include "ShaderInjectorGUI.h"
#include "VTableIndex.h"

namespace HookD3D12
{
	static std::unordered_set<ID3D12PipelineLibrary*> gHookedPipelineLibraries;

	HRESULT __stdcall Hook_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		HRESULT hr = Original_LoadGraphicsPipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CaptureGraphicsPipelineState(desc, (ID3D12PipelineState*)*ppPipelineState);
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_LoadGraphicsPipeline: PipelineLibrary LoadGraphicsPipeline captured cached PSO");
		}

		return hr;
	}

	HRESULT __stdcall Hook_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		HRESULT hr = Original_LoadComputePipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CaptureComputePipelineState(desc, (ID3D12PipelineState*)*ppPipelineState, false);
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_LoadComputePipeline: PipelineLibrary LoadComputePipeline captured cached PSO");
		}

		return hr;
	}

	HRESULT __stdcall Hook_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		HRESULT hr = Original_LoadPipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CapturePipelineStateStream(desc, (ID3D12PipelineState*)*ppPipelineState);
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_LoadPipeline: PipelineLibrary1 LoadPipeline captured cached stream PSO");
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

		if (gHookedPipelineLibraries.find(pipelineLibrary) != gHookedPipelineLibraries.end())
			return;

		gHookedPipelineLibraries.insert(pipelineLibrary);

		void** vtable = *(void***)(pipelineLibrary);

		MH_CreateHook(vtable[VTableIndex::indexStorePipeline], Hook_StorePipeline, reinterpret_cast<void**>(&Original_StorePipeline));
		MH_CreateHook(vtable[VTableIndex::indexLoadGraphicsPipeline], Hook_LoadGraphicsPipeline, reinterpret_cast<void**>(&Original_LoadGraphicsPipeline));
		MH_CreateHook(vtable[VTableIndex::indexLoadComputePipeline], Hook_LoadComputePipeline, reinterpret_cast<void**>(&Original_LoadComputePipeline));
		MH_CreateHook(vtable[VTableIndex::indexGetSerializedSize], Hook_GetSerializedSize, reinterpret_cast<void**>(&Original_GetSerializedSize));
		MH_CreateHook(vtable[VTableIndex::indexSerialize], Hook_Serialize, reinterpret_cast<void**>(&Original_Serialize));

		ID3D12PipelineLibrary1* pipelineLibrary1 = nullptr;

		if (SUCCEEDED(pipelineLibrary->QueryInterface(IID_PPV_ARGS(&pipelineLibrary1))))
		{
			void** vtable1 = *(void***)(pipelineLibrary1);
			MH_CreateHook(vtable1[VTableIndex::indexLoadPipeline], Hook_LoadPipeline, reinterpret_cast<void**>(&Original_LoadPipeline));
			pipelineLibrary1->Release();
		}

		MH_EnableHook(MH_ALL_HOOKS);

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->HookPipelineLibrary: Pipeline library hooks installed");
	}

	HRESULT __stdcall Hook_CreatePipelineLibrary(ID3D12Device1* device, const void* pLibraryBlob, SIZE_T blobLength, REFIID riid, void** ppPipelineLibrary)
	{
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineLibrary->Hook_CreatePipelineLibrary: CreatePipelineLibrary Blob Size: " + std::to_string(blobLength));

		HRESULT hr = Original_CreatePipelineLibrary(device, pLibraryBlob, blobLength, riid, ppPipelineLibrary);

		if (SUCCEEDED(hr) && ppPipelineLibrary && *ppPipelineLibrary)
		{
			auto* pipelineLibrary = (ID3D12PipelineLibrary*)(*ppPipelineLibrary);
			HookPipelineLibrary(pipelineLibrary);
		}

		return hr;
	}
}
