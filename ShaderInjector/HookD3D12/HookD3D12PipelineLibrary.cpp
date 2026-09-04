//HookD3D12PipelineLibrary.cpp
#include "HookD3D12.h"

#include <unordered_set>

//3RD Party
#include "MinHook.h"

//custom
#include "ShaderInjectorGUI.h"
#include "VTableIndex.h"

namespace HookD3D12
{
	static std::unordered_set<ID3D12PipelineLibrary*> gHookedPipelineLibraries;

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

}
