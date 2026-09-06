//HookD3D12Install.cpp
#include "HookD3D12.h"

#include <string>
#include <unordered_set>

//3RD Party
#include "MinHook.h"

//custom
#include "ShaderInjectorGUI.h"
#include "SystemInfoLogger.h"
#include "VTableIndex.h"
#include "NativeVTableHooks.h"

namespace HookD3D12
{
	static bool checkD3D12CreateDeviceHookInstalled = false;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL D3D12 CREATE DEVICE HOOK |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL D3D12 CREATE DEVICE HOOK |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL D3D12 CREATE DEVICE HOOK |||||||||||||||||||||||||||||||||||||||||||||||||||||

	bool InstallD3D12CreateDeviceHook(HMODULE d3d12Module)
	{
		if (checkD3D12CreateDeviceHookInstalled)
			return true;

		if (!d3d12Module)
			return false;

		void* createDeviceAddress = reinterpret_cast<void*>(GetProcAddress(d3d12Module, "D3D12CreateDevice"));

		if (!createDeviceAddress)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallD3D12CreateDeviceHook: D3D12CreateDevice export not found");
			return false;
		}

		MH_STATUS createStatus = MH_CreateHook(createDeviceAddress, reinterpret_cast<void*>(&Hook_CreateDeviceD3D12), reinterpret_cast<void**>(&Original_CreateDeviceD3D12));

		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(std::string("HookD3D12Install->InstallD3D12CreateDeviceHook: D3D12CreateDevice hook create failed: ") + MH_StatusToString(createStatus));
			return false;
		}

		MH_STATUS enableStatus = MH_EnableHook(createDeviceAddress);

		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(std::string("HookD3D12Install->InstallD3D12CreateDeviceHook: D3D12CreateDevice hook enable failed: ") + MH_StatusToString(enableStatus));
			return false;
		}

		checkD3D12CreateDeviceHookInstalled = true;
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallD3D12CreateDeviceHook: D3D12CreateDevice hook installed");
		return true;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK D3D12 CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK D3D12 CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK D3D12 CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT WINAPI Hook_CreateDeviceD3D12(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice)
	{
		HRESULT createDeviceResult = Original_CreateDeviceD3D12(pAdapter, MinimumFeatureLevel, riid, ppDevice);

		if (SUCCEEDED(createDeviceResult) && ppDevice && *ppDevice)
		{
			ID3D12Device* device = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*ppDevice);

			if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&device))))
			{
				InstallPipelineHooksForDevice(device);
				SystemInfoLogger::LogD3D12DeviceInfo(device);
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->Hook_CreateDeviceD3D12: D3D12CreateDevice captured device and installed pipeline hooks");
				device->Release();
			}
		}

		return createDeviceResult;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL PIPELINE HOOKS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL PIPELINE HOOKS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL PIPELINE HOOKS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using CreateCommandQueueFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
	using CreateCommandQueue1Function = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, REFIID, void**);
	using CreateCommandListFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**);
	using CreateCommandList1Function = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void**);
	static CreateCommandQueueFunction originalCreateCommandQueue = nullptr;
	static CreateCommandQueue1Function originalCreateCommandQueue1 = nullptr;
	static CreateCommandListFunction originalCreateCommandList = nullptr;
	static CreateCommandList1Function originalCreateCommandList1 = nullptr;

	void InstallCommandQueueHooksForCommandQueue(ID3D12CommandQueue* queue)
	{
		if (!queue) return;
		NativeVTableHooks::Install(queue, NativeVTableHooks::Family::Queue,
			VTableIndex::indexExecuteCommandLists, Hook_ExecuteCommandListsD3D12,
			&Original_ExecuteCommandListsD3D12, "ExecuteCommandLists");
	}

	static void CaptureCreatedCommandQueue(HRESULT result, void** output)
	{
		if (FAILED(result) || !output || !*output) return;
		ID3D12CommandQueue* queue = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&queue)))) {
			InstallCommandQueueHooksForCommandQueue(queue);
			queue->Release();
		}
	}

	static void CaptureCreatedCommandList(HRESULT result, void** output)
	{
		if (FAILED(result) || !output || !*output) return;
		ID3D12GraphicsCommandList* list = nullptr;
		if (SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&list)))) {
			InstallCommandListHooksForCommandList(list);
			list->Release();
		}
	}

	static HRESULT STDMETHODCALLTYPE Hook_CreateCommandQueue(ID3D12Device* device,
		const D3D12_COMMAND_QUEUE_DESC* desc, REFIID iid, void** output)
	{
		HRESULT result = originalCreateCommandQueue(device, desc, iid, output);
		CaptureCreatedCommandQueue(result, output);
		return result;
	}
	static HRESULT STDMETHODCALLTYPE Hook_CreateCommandQueue1(ID3D12Device* device,
		const D3D12_COMMAND_QUEUE_DESC* desc, REFIID creator, REFIID iid, void** output)
	{
		HRESULT result = originalCreateCommandQueue1(device, desc, creator, iid, output);
		CaptureCreatedCommandQueue(result, output);
		return result;
	}
	static HRESULT STDMETHODCALLTYPE Hook_CreateCommandList(ID3D12Device* device, UINT nodeMask,
		D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* allocator,
		ID3D12PipelineState* initialState, REFIID iid, void** output)
	{
		HRESULT result = originalCreateCommandList(device, nodeMask, type, allocator, initialState, iid, output);
		CaptureCreatedCommandList(result, output);
		return result;
	}
	static HRESULT STDMETHODCALLTYPE Hook_CreateCommandList1(ID3D12Device* device, UINT nodeMask,
		D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags, REFIID iid, void** output)
	{
		HRESULT result = originalCreateCommandList1(device, nodeMask, type, flags, iid, output);
		CaptureCreatedCommandList(result, output);
		return result;
	}

	void InstallPipelineHooksForDevice(ID3D12Device* device)
	{
		if (!device) return;
		using NativeVTableHooks::Family;
		NativeVTableHooks::Install(device, Family::Device, VTableIndex::indexCreateGraphicsPipelineState,
			Hook_CreateGraphicsPipelineState, &Original_CreateGraphicsPipelineState, "CreateGraphicsPipelineState");
		NativeVTableHooks::Install(device, Family::Device, VTableIndex::indexCreateComputePipelineState,
			Hook_CreateComputePipelineState, &Original_CreateComputePipelineState, "CreateComputePipelineState");
		NativeVTableHooks::Install(device, Family::Device, VTableIndex::indexCreateRootSignature,
			Hook_CreateRootSignature, &Original_CreateRootSignature, "CreateRootSignature");
		ID3D12Device2* device2 = nullptr;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2)))) {
			NativeVTableHooks::Install(device2, Family::Device, VTableIndex::indexCreatePipelineState,
				Hook_CreatePipelineState, &Original_CreatePipelineState, "CreatePipelineState");
			device2->Release();
		}
		ID3D12Device1* device1 = nullptr;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
			NativeVTableHooks::Install(device1, Family::Device, VTableIndex::indexCreatePipelineLibrary,
				Hook_CreatePipelineLibrary, &Original_CreatePipelineLibrary, "CreatePipelineLibrary");
			device1->Release();
		}
		if (NativeVTableHooks::IsNativeObject(device)) {
			// Object-local shadows do not cover subsequently created objects.
			// Intercept both legacy and newer creation APIs before returning them.
			NativeVTableHooks::Install(device, Family::Device, 8, Hook_CreateCommandQueue,
				&originalCreateCommandQueue, "CreateCommandQueue");
			NativeVTableHooks::Install(device, Family::Device, 12, Hook_CreateCommandList,
				&originalCreateCommandList, "CreateCommandList");
			const size_t entries = NativeVTableHooks::AdvertisedLength(device, Family::Device);
			if (entries > 51)
				NativeVTableHooks::Install(device, Family::Device, 51, Hook_CreateCommandList1,
					&originalCreateCommandList1, "CreateCommandList1");
			if (entries > 75)
				NativeVTableHooks::Install(device, Family::Device, 75, Hook_CreateCommandQueue1,
					&originalCreateCommandQueue1, "CreateCommandQueue1");
		}
	}

	void InstallCommandListHooksForCommandList(ID3D12GraphicsCommandList* commandList)
	{
		if (!commandList) return;
		// This path also runs at submission to discover lists created before
		// startup. Four pointer reads avoid repeated QI or registry work once
		// this implementation's complete shadow is attached to the object.
		void** table = *reinterpret_cast<void***>(commandList);
		if (table[VTableIndex::indexResetGraphicsCommandList] == reinterpret_cast<void*>(Hook_ResetGraphicsCommandList) &&
			table[VTableIndex::indexSetPipelineState] == reinterpret_cast<void*>(Hook_SetPipelineState) &&
			table[VTableIndex::indexSetComputeRootSignature] == reinterpret_cast<void*>(Hook_SetComputeRootSignature) &&
			table[VTableIndex::indexSetGraphicsRootSignature] == reinterpret_cast<void*>(Hook_SetGraphicsRootSignature))
			return;
		using NativeVTableHooks::Family;
		NativeVTableHooks::Install(commandList, Family::CommandList, VTableIndex::indexResetGraphicsCommandList,
			Hook_ResetGraphicsCommandList, &Original_ResetGraphicsCommandList, "CommandList Reset");
		NativeVTableHooks::Install(commandList, Family::CommandList, VTableIndex::indexSetPipelineState,
			Hook_SetPipelineState, &Original_SetPipelineState, "SetPipelineState");
		NativeVTableHooks::Install(commandList, Family::CommandList, VTableIndex::indexSetComputeRootSignature,
			Hook_SetComputeRootSignature, &Original_SetComputeRootSignature, "SetComputeRootSignature");
		NativeVTableHooks::Install(commandList, Family::CommandList, VTableIndex::indexSetGraphicsRootSignature,
			Hook_SetGraphicsRootSignature, &Original_SetGraphicsRootSignature, "SetGraphicsRootSignature");
	}
}
