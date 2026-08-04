//HookD3D12Install.cpp
#include "HookD3D12.h"

#include <mutex>
#include <string>
#include <unordered_set>

//3RD Party
#include "MinHook.h"

//custom
#include "ShaderInjectorGUI.h"
#include "SystemInfoLogger.h"
#include "VTableIndex.h"
#include "HookD3D12RenderPass.h"
#include "HookD3D12Resources.h"
#include "RenderPassRuntime.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"

namespace HookD3D12
{
	static bool checkD3D12CreateDeviceHookInstalled = false;
	static bool checkCommandListHookInstalled = false;
	static std::mutex hookInstallationMutex;
	static std::unordered_set<void*> graphicsPipelineHookedDeviceVTables;
	static std::unordered_set<void*> graphicsCommandListHookedVTables;
	static std::unordered_set<void*> renderPassHookedDeviceVTables;
	static std::unordered_set<void*> renderPassHookedCommandListVTables;
	static void** capturedGraphicsCommandListVTable = nullptr;

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
				InstallRenderPassResourceHooksForDevice(device);
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

	void InstallPipelineHooksForDevice(ID3D12Device* device)
	{
		if (!device)
			return;

		std::lock_guard<std::mutex> installationLock(hookInstallationMutex);

		void** deviceVTable = *reinterpret_cast<void***>(device);
		void* deviceVTableKey = deviceVTable;

		if (!graphicsPipelineHookedDeviceVTables.insert(deviceVTableKey).second)
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
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallPipelineHooksForDevice: CreatePipelineState hook installed");
			else
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallPipelineHooksForDevice: CreatePipelineState hook failed");

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
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallPipelineHooksForDevice: CreatePipelineLibrary hook installed");
			else
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallPipelineHooksForDevice: CreatePipelineLibrary hook failed");
		}

		if (statusGraphicsPipelineCreate == MH_OK && statusGraphicsPipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallPipelineHooksForDevice: CreateGraphicsPipelineState hook installed");
		else
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallPipelineHooksForDevice: CreateGraphicsPipelineState hook failed");

		if (statusComputePipelineCreate == MH_OK && statusComputePipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallPipelineHooksForDevice: CreateComputePipelineState hook installed");
		else
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallPipelineHooksForDevice: CreateComputePipelineState hook failed");

		if (statusRootSignatureCreate == MH_OK && statusRootSignatureEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallPipelineHooksForDevice: CreateRootSignature hook installed");
		else
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallPipelineHooksForDevice: CreateRootSignature hook failed");
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL COMMAND LIST HOOKS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL COMMAND LIST HOOKS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INSTALL COMMAND LIST HOOKS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void InstallCommandListHooksForCommandList(ID3D12GraphicsCommandList* commandList)
	{
		if (!commandList)
			return;

		std::lock_guard<std::mutex> installationLock(hookInstallationMutex);

		void** commandListVTable = *reinterpret_cast<void***>(commandList);
		void* commandListVTableKey = commandListVTable;
		capturedGraphicsCommandListVTable = commandListVTable;

		if (!graphicsCommandListHookedVTables.insert(commandListVTableKey).second)
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
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallCommandListHooksForCommandList: CommandList Reset hook installed");
		else
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallCommandListHooksForCommandList: CommandList Reset hook failed");

		if (setPipelineCreate == MH_OK && setPipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallCommandListHooksForCommandList: SetPipelineState hook installed");
		else
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallCommandListHooksForCommandList: SetPipelineState hook failed");

		if (setComputeRootCreate == MH_OK && setComputeRootEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallCommandListHooksForCommandList: SetComputeRootSignature hook installed");
		else
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallCommandListHooksForCommandList: SetComputeRootSignature hook failed");

		if (setGraphicsRootCreate == MH_OK && setGraphicsRootEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->InstallCommandListHooksForCommandList: SetGraphicsRootSignature hook installed");
		else
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12Install->InstallCommandListHooksForCommandList: SetGraphicsRootSignature hook failed");

			checkCommandListHookInstalled = true;
	}

	void InstallRenderPassResourceHooksForDevice(ID3D12Device* device)
	{
		if (!device || !RenderPassRuntime::HasEnabledMipChainPasses())
			return;

		std::lock_guard<std::mutex> installationLock(hookInstallationMutex);
		void** deviceVTable = *reinterpret_cast<void***>(device);
		void* deviceVTableKey = deviceVTable;
		if (renderPassHookedDeviceVTables.find(deviceVTableKey) != renderPassHookedDeviceVTables.end())
			return;

		struct HookDefinition
		{
			size_t vtableIndex;
			void* hookFunction;
			void** originalFunction;
		};

		const HookDefinition resourceHooks[] =
		{
			{ VTableIndex::indexCreateConstantBufferView, reinterpret_cast<void*>(&Hook_CreateConstantBufferView), reinterpret_cast<void**>(&Original_CreateConstantBufferView) },
			{ VTableIndex::indexCreateShaderResourceView, reinterpret_cast<void*>(&Hook_CreateShaderResourceView), reinterpret_cast<void**>(&Original_CreateShaderResourceView) },
			{ VTableIndex::indexCreateUnorderedAccessView, reinterpret_cast<void*>(&Hook_CreateUnorderedAccessView), reinterpret_cast<void**>(&Original_CreateUnorderedAccessView) },
			{ VTableIndex::indexCreateRenderTargetView, reinterpret_cast<void*>(&Hook_CreateRenderTargetView), reinterpret_cast<void**>(&Original_CreateRenderTargetView) },
			{ VTableIndex::indexCreateDepthStencilView, reinterpret_cast<void*>(&Hook_CreateDepthStencilView), reinterpret_cast<void**>(&Original_CreateDepthStencilView) },
			{ VTableIndex::indexCreateSampler, reinterpret_cast<void*>(&Hook_CreateSampler), reinterpret_cast<void**>(&Original_CreateSampler) },
			{ VTableIndex::indexCopyDescriptors, reinterpret_cast<void*>(&Hook_CopyDescriptors), reinterpret_cast<void**>(&Original_CopyDescriptors) },
			{ VTableIndex::indexCopyDescriptorsSimple, reinterpret_cast<void*>(&Hook_CopyDescriptorsSimple), reinterpret_cast<void**>(&Original_CopyDescriptorsSimple) },
			{ VTableIndex::indexCreateCommittedResource, reinterpret_cast<void*>(&Hook_CreateCommittedResource), reinterpret_cast<void**>(&Original_CreateCommittedResource) },
			{ VTableIndex::indexCreatePlacedResource, reinterpret_cast<void*>(&Hook_CreatePlacedResource), reinterpret_cast<void**>(&Original_CreatePlacedResource) },
			{ VTableIndex::indexCreateReservedResource, reinterpret_cast<void*>(&Hook_CreateReservedResource), reinterpret_cast<void**>(&Original_CreateReservedResource) },
		};

		bool resourceHooksInstalled = true;
		for (const HookDefinition& hook : resourceHooks)
		{
			const MH_STATUS createStatus = MH_CreateHook(
				deviceVTable[hook.vtableIndex],
				hook.hookFunction,
				hook.originalFunction);
			const MH_STATUS enableStatus = MH_EnableHook(deviceVTable[hook.vtableIndex]);
			const bool createSucceeded = createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED;
			const bool enableSucceeded = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
			resourceHooksInstalled = resourceHooksInstalled && createSucceeded && enableSucceeded;
		}

		if (resourceHooksInstalled)
		{
			renderPassHookedDeviceVTables.insert(deviceVTableKey);
			ShaderInjectorIO::WriteToLogFileSuccess(
				"HookD3D12Install->InstallRenderPassResourceHooksForDevice: mip resource hooks installed");
		}
		else
		{
			ShaderInjectorIO::WriteToLogFileError(
				"HookD3D12Install->InstallRenderPassResourceHooksForDevice: one or more resource hooks failed");
		}
	}

	void InstallDeferredRenderPassHooks(ID3D12Device* device)
	{
		InstallRenderPassResourceHooksForDevice(device);

		// The game performs its most intensive pipeline/query work before the overlay is
		// ready. Render-pass observation is unnecessary during that phase, especially on
		// a fresh shader-cache run where no shader target can be resolved yet.
		if (!device || !capturedGraphicsCommandListVTable || !RenderPassRuntime::HasEnabledRenderPasses())
			return;

		std::lock_guard<std::mutex> installationLock(hookInstallationMutex);

		struct HookDefinition
		{
			size_t vtableIndex;
			void* hookFunction;
			void** originalFunction;
		};

		void** deviceVTable = *reinterpret_cast<void***>(device);
		void* deviceVTableKey = deviceVTable;
		if (renderPassHookedDeviceVTables.find(deviceVTableKey) == renderPassHookedDeviceVTables.end())
		{
			const HookDefinition resourceHooks[] =
			{
				{ VTableIndex::indexCreateConstantBufferView, reinterpret_cast<void*>(&Hook_CreateConstantBufferView), reinterpret_cast<void**>(&Original_CreateConstantBufferView) },
				{ VTableIndex::indexCreateShaderResourceView, reinterpret_cast<void*>(&Hook_CreateShaderResourceView), reinterpret_cast<void**>(&Original_CreateShaderResourceView) },
				{ VTableIndex::indexCreateUnorderedAccessView, reinterpret_cast<void*>(&Hook_CreateUnorderedAccessView), reinterpret_cast<void**>(&Original_CreateUnorderedAccessView) },
				{ VTableIndex::indexCreateRenderTargetView, reinterpret_cast<void*>(&Hook_CreateRenderTargetView), reinterpret_cast<void**>(&Original_CreateRenderTargetView) },
				{ VTableIndex::indexCreateDepthStencilView, reinterpret_cast<void*>(&Hook_CreateDepthStencilView), reinterpret_cast<void**>(&Original_CreateDepthStencilView) },
				{ VTableIndex::indexCreateSampler, reinterpret_cast<void*>(&Hook_CreateSampler), reinterpret_cast<void**>(&Original_CreateSampler) },
				{ VTableIndex::indexCopyDescriptors, reinterpret_cast<void*>(&Hook_CopyDescriptors), reinterpret_cast<void**>(&Original_CopyDescriptors) },
				{ VTableIndex::indexCopyDescriptorsSimple, reinterpret_cast<void*>(&Hook_CopyDescriptorsSimple), reinterpret_cast<void**>(&Original_CopyDescriptorsSimple) },
				{ VTableIndex::indexCreateCommittedResource, reinterpret_cast<void*>(&Hook_CreateCommittedResource), reinterpret_cast<void**>(&Original_CreateCommittedResource) },
				{ VTableIndex::indexCreatePlacedResource, reinterpret_cast<void*>(&Hook_CreatePlacedResource), reinterpret_cast<void**>(&Original_CreatePlacedResource) },
				{ VTableIndex::indexCreateReservedResource, reinterpret_cast<void*>(&Hook_CreateReservedResource), reinterpret_cast<void**>(&Original_CreateReservedResource) },
			};

			bool resourceHooksInstalled = true;
			for (const HookDefinition& hook : resourceHooks)
			{
				const MH_STATUS createStatus = MH_CreateHook(
					deviceVTable[hook.vtableIndex],
					hook.hookFunction,
					hook.originalFunction);
				const MH_STATUS enableStatus = MH_EnableHook(deviceVTable[hook.vtableIndex]);
				const bool createSucceeded = createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED;
				const bool enableSucceeded = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
				resourceHooksInstalled = resourceHooksInstalled && createSucceeded && enableSucceeded;
			}

			if (resourceHooksInstalled)
			{
				renderPassHookedDeviceVTables.insert(deviceVTableKey);
				ShaderInjectorIO::WriteToLogFileSuccess(
					"HookD3D12Install->InstallDeferredRenderPassHooks: resource hooks installed after overlay readiness");
			}
			else
			{
				ShaderInjectorIO::WriteToLogFileError(
					"HookD3D12Install->InstallDeferredRenderPassHooks: one or more deferred resource hooks failed");
			}
		}

		void** commandListVTable = capturedGraphicsCommandListVTable;
		void* commandListVTableKey = commandListVTable;
		if (renderPassHookedCommandListVTables.find(commandListVTableKey) == renderPassHookedCommandListVTables.end())
		{
			const HookDefinition commandListHooks[] =
			{
				{ VTableIndex::indexDrawInstanced, reinterpret_cast<void*>(&Hook_DrawInstanced), reinterpret_cast<void**>(&Original_DrawInstanced) },
				{ VTableIndex::indexDrawIndexedInstanced, reinterpret_cast<void*>(&Hook_DrawIndexedInstanced), reinterpret_cast<void**>(&Original_DrawIndexedInstanced) },
				{ VTableIndex::indexDispatch, reinterpret_cast<void*>(&Hook_Dispatch), reinterpret_cast<void**>(&Original_Dispatch) },
				{ VTableIndex::indexIASetPrimitiveTopology, reinterpret_cast<void*>(&Hook_IASetPrimitiveTopology), reinterpret_cast<void**>(&Original_IASetPrimitiveTopology) },
				{ VTableIndex::indexRSSetViewports, reinterpret_cast<void*>(&Hook_RSSetViewports), reinterpret_cast<void**>(&Original_RSSetViewports) },
				{ VTableIndex::indexRSSetScissorRects, reinterpret_cast<void*>(&Hook_RSSetScissorRects), reinterpret_cast<void**>(&Original_RSSetScissorRects) },
				{ VTableIndex::indexSetDescriptorHeaps, reinterpret_cast<void*>(&Hook_SetDescriptorHeaps), reinterpret_cast<void**>(&Original_SetDescriptorHeaps) },
				{ VTableIndex::indexSetComputeRootDescriptorTable, reinterpret_cast<void*>(&Hook_SetComputeRootDescriptorTable), reinterpret_cast<void**>(&Original_SetComputeRootDescriptorTable) },
				{ VTableIndex::indexSetGraphicsRootDescriptorTable, reinterpret_cast<void*>(&Hook_SetGraphicsRootDescriptorTable), reinterpret_cast<void**>(&Original_SetGraphicsRootDescriptorTable) },
				{ VTableIndex::indexSetComputeRoot32BitConstant, reinterpret_cast<void*>(&Hook_SetComputeRoot32BitConstant), reinterpret_cast<void**>(&Original_SetComputeRoot32BitConstant) },
				{ VTableIndex::indexSetGraphicsRoot32BitConstant, reinterpret_cast<void*>(&Hook_SetGraphicsRoot32BitConstant), reinterpret_cast<void**>(&Original_SetGraphicsRoot32BitConstant) },
				{ VTableIndex::indexSetComputeRoot32BitConstants, reinterpret_cast<void*>(&Hook_SetComputeRoot32BitConstants), reinterpret_cast<void**>(&Original_SetComputeRoot32BitConstants) },
				{ VTableIndex::indexSetGraphicsRoot32BitConstants, reinterpret_cast<void*>(&Hook_SetGraphicsRoot32BitConstants), reinterpret_cast<void**>(&Original_SetGraphicsRoot32BitConstants) },
				{ VTableIndex::indexSetComputeRootConstantBufferView, reinterpret_cast<void*>(&Hook_SetComputeRootConstantBufferView), reinterpret_cast<void**>(&Original_SetComputeRootConstantBufferView) },
				{ VTableIndex::indexSetGraphicsRootConstantBufferView, reinterpret_cast<void*>(&Hook_SetGraphicsRootConstantBufferView), reinterpret_cast<void**>(&Original_SetGraphicsRootConstantBufferView) },
				{ VTableIndex::indexSetComputeRootShaderResourceView, reinterpret_cast<void*>(&Hook_SetComputeRootShaderResourceView), reinterpret_cast<void**>(&Original_SetComputeRootShaderResourceView) },
				{ VTableIndex::indexSetGraphicsRootShaderResourceView, reinterpret_cast<void*>(&Hook_SetGraphicsRootShaderResourceView), reinterpret_cast<void**>(&Original_SetGraphicsRootShaderResourceView) },
				{ VTableIndex::indexSetComputeRootUnorderedAccessView, reinterpret_cast<void*>(&Hook_SetComputeRootUnorderedAccessView), reinterpret_cast<void**>(&Original_SetComputeRootUnorderedAccessView) },
				{ VTableIndex::indexSetGraphicsRootUnorderedAccessView, reinterpret_cast<void*>(&Hook_SetGraphicsRootUnorderedAccessView), reinterpret_cast<void**>(&Original_SetGraphicsRootUnorderedAccessView) },
				{ VTableIndex::indexIASetIndexBuffer, reinterpret_cast<void*>(&Hook_IASetIndexBuffer), reinterpret_cast<void**>(&Original_IASetIndexBuffer) },
				{ VTableIndex::indexIASetVertexBuffers, reinterpret_cast<void*>(&Hook_IASetVertexBuffers), reinterpret_cast<void**>(&Original_IASetVertexBuffers) },
				{ VTableIndex::indexOMSetRenderTargets, reinterpret_cast<void*>(&Hook_OMSetRenderTargets), reinterpret_cast<void**>(&Original_OMSetRenderTargets) },
				{ VTableIndex::indexExecuteIndirect, reinterpret_cast<void*>(&Hook_ExecuteIndirect), reinterpret_cast<void**>(&Original_ExecuteIndirect) },
			};

			bool commandListHooksInstalled = true;
			for (const HookDefinition& hook : commandListHooks)
			{
				const MH_STATUS createStatus = MH_CreateHook(
					commandListVTable[hook.vtableIndex],
					hook.hookFunction,
					hook.originalFunction);
				const MH_STATUS enableStatus = MH_EnableHook(commandListVTable[hook.vtableIndex]);
				const bool createSucceeded = createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED;
				const bool enableSucceeded = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
				commandListHooksInstalled = commandListHooksInstalled && createSucceeded && enableSucceeded;
			}

			if (commandListHooksInstalled)
			{
				renderPassHookedCommandListVTables.insert(commandListVTableKey);
				ShaderInjectorIO::WriteToLogFileSuccess(StringHelper::Format(
					"HookD3D12Install->InstallDeferredRenderPassHooks: command-list hooks installed after overlay readiness vtable=%p",
					commandListVTable));
			}
			else
			{
				ShaderInjectorIO::WriteToLogFileError(
					"HookD3D12Install->InstallDeferredRenderPassHooks: one or more deferred command-list hooks failed");
			}
		}
	}
}
