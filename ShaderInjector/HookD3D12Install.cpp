#include "HookD3D12.h"

#include <string>
#include <unordered_set>

#include "MinHook.h"
#include "ShaderInjectorGUI.h"
#include "VTableIndex.h"

namespace HookD3D12
{
	static bool gD3D12CreateDeviceHookInstalled = false;
	static bool gCommandListHookInstalled = false;
	static std::unordered_set<void*> gPipelineHookedDeviceVTables;
	static std::unordered_set<void*> gCommandListHookedVTables;

	void InstallPipelineHooksForDevice(ID3D12Device* device)
	{
		if (!device)
			return;

		void** deviceVTable = *reinterpret_cast<void***>(device);
		void* deviceVTableKey = deviceVTable;

		if (!gPipelineHookedDeviceVTables.insert(deviceVTableKey).second)
			return;

		MH_STATUS statusGraphicsPipelineCreate = MH_CreateHook(deviceVTable[VTableIndex::indexCreateGraphicsPipelineState], &Hook_CreateGraphicsPipelineState, reinterpret_cast<void**>(&Original_CreateGraphicsPipelineState));
		MH_STATUS statusGraphicsPipelineEnable = MH_EnableHook(deviceVTable[VTableIndex::indexCreateGraphicsPipelineState]);

		MH_STATUS statusComputePipelineCreate = MH_CreateHook(deviceVTable[VTableIndex::indexCreateComputePipelineState], &Hook_CreateComputePipelineState, reinterpret_cast<void**>(&Original_CreateComputePipelineState));
		MH_STATUS statusComputePipelineEnable = MH_EnableHook(deviceVTable[VTableIndex::indexCreateComputePipelineState]);

		MH_STATUS statusRootSignatureCreate = MH_CreateHook(deviceVTable[VTableIndex::indexCreateRootSignature], &Hook_CreateRootSignature, reinterpret_cast<void**>(&Original_CreateRootSignature));
		MH_STATUS statusRootSignatureEnable = MH_EnableHook(deviceVTable[VTableIndex::indexCreateRootSignature]);

		ID3D12Device2* device2 = nullptr;

		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2))))
		{
			void** device2VTable = *reinterpret_cast<void***>(device2);

			MH_STATUS statusCreatePipelineStateCreate = MH_CreateHook(device2VTable[VTableIndex::indexCreatePipelineState], &Hook_CreatePipelineState, reinterpret_cast<void**>(&Original_CreatePipelineState));
			MH_STATUS statusCreatePipelineStateEnable = MH_EnableHook(device2VTable[VTableIndex::indexCreatePipelineState]);

			if (statusCreatePipelineStateCreate == MH_OK && statusCreatePipelineStateEnable == MH_OK)
				ShaderInjectorGUI::WriteToRuntimeLog("CreatePipelineState hook installed");

			device2->Release();
		}

		ID3D12Device1* device1 = nullptr;

		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1))))
		{
			void** device1VTable = *reinterpret_cast<void***>(device1);

			MH_STATUS statusCreatePipelineLibraryCreate = MH_CreateHook(device1VTable[VTableIndex::indexCreatePipelineLibrary], &Hook_CreatePipelineLibrary, reinterpret_cast<void**>(&Original_CreatePipelineLibrary));
			MH_STATUS statusCreatePipelineLibraryEnable = MH_EnableHook(device1VTable[VTableIndex::indexCreatePipelineLibrary]);

			device1->Release();

			if (statusCreatePipelineLibraryCreate == MH_OK && statusCreatePipelineLibraryEnable == MH_OK)
				ShaderInjectorGUI::WriteToRuntimeLog("CreatePipelineLibrary hook installed");
		}

		if (statusGraphicsPipelineCreate == MH_OK && statusGraphicsPipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("CreateGraphicsPipelineState hook installed");

		if (statusComputePipelineCreate == MH_OK && statusComputePipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("CreateComputePipelineState hook installed");

		if (statusRootSignatureCreate == MH_OK && statusRootSignatureEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("CreateRootSignature hook installed");
	}

	HRESULT WINAPI Hook_D3D12CreateDevice(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice)
	{
		HRESULT hr = Original_D3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);

		if (SUCCEEDED(hr) && ppDevice && *ppDevice)
		{
			ID3D12Device* device = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*ppDevice);

			if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&device))))
			{
				InstallPipelineHooksForDevice(device);
				ShaderInjectorGUI::WriteToRuntimeLog("D3D12CreateDevice captured device and installed pipeline hooks");
				device->Release();
			}
		}

		return hr;
	}

	bool InstallD3D12CreateDeviceHook(HMODULE d3d12Module)
	{
		if (gD3D12CreateDeviceHookInstalled)
			return true;

		if (!d3d12Module)
			return false;

		void* createDeviceAddress = reinterpret_cast<void*>(GetProcAddress(d3d12Module, "D3D12CreateDevice"));

		if (!createDeviceAddress)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice export not found");
			return false;
		}

		MH_STATUS createStatus = MH_CreateHook(createDeviceAddress, reinterpret_cast<void*>(&Hook_D3D12CreateDevice), reinterpret_cast<void**>(&Original_D3D12CreateDevice));

		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice hook create failed: ") + MH_StatusToString(createStatus));
			return false;
		}

		MH_STATUS enableStatus = MH_EnableHook(createDeviceAddress);

		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice hook enable failed: ") + MH_StatusToString(enableStatus));
			return false;
		}

		gD3D12CreateDeviceHookInstalled = true;
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice hook installed");
		return true;
	}

	void InstallCommandListHooksForCommandList(ID3D12GraphicsCommandList* commandList)
	{
		if (!commandList)
			return;

		void** commandListVTable = *reinterpret_cast<void***>(commandList);
		void* commandListVTableKey = commandListVTable;

		if (!gCommandListHookedVTables.insert(commandListVTableKey).second)
			return;

		MH_STATUS resetCreate = MH_CreateHook(commandListVTable[VTableIndex::indexResetGraphicsCommandList], &Hook_ResetGraphicsCommandList, reinterpret_cast<void**>(&Original_ResetGraphicsCommandList));
		MH_STATUS resetEnable = MH_EnableHook(commandListVTable[VTableIndex::indexResetGraphicsCommandList]);

		MH_STATUS setPipelineCreate = MH_CreateHook(commandListVTable[VTableIndex::indexSetPipelineState], &Hook_SetPipelineState, reinterpret_cast<void**>(&Original_SetPipelineState));
		MH_STATUS setPipelineEnable = MH_EnableHook(commandListVTable[VTableIndex::indexSetPipelineState]);

		MH_STATUS setComputeRootCreate = MH_CreateHook(commandListVTable[VTableIndex::indexSetComputeRootSignature], &Hook_SetComputeRootSignature, reinterpret_cast<void**>(&Original_SetComputeRootSignature));
		MH_STATUS setComputeRootEnable = MH_EnableHook(commandListVTable[VTableIndex::indexSetComputeRootSignature]);

		MH_STATUS setGraphicsRootCreate = MH_CreateHook(commandListVTable[VTableIndex::indexSetGraphicsRootSignature], &Hook_SetGraphicsRootSignature, reinterpret_cast<void**>(&Original_SetGraphicsRootSignature));
		MH_STATUS setGraphicsRootEnable = MH_EnableHook(commandListVTable[VTableIndex::indexSetGraphicsRootSignature]);

		if (resetCreate == MH_OK && resetEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallCommandListHooksForCommandList | CommandList Reset hook installed");

		if (setPipelineCreate == MH_OK && setPipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallCommandListHooksForCommandList | SetPipelineState hook installed");

		if (setComputeRootCreate == MH_OK && setComputeRootEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallCommandListHooksForCommandList | SetComputeRootSignature hook installed");

		if (setGraphicsRootCreate == MH_OK && setGraphicsRootEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | HandlePresentD3D12 | SetGraphicsRootSignature hook installed");

		gCommandListHookInstalled = true;
	}
}
