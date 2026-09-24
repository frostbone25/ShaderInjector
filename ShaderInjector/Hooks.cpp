//Hooks.cpp
#include "wrl/client.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <dxgi.h>
#include <d3d12.h>

//3RD Party
#include "MinHook.h"

//custom
#include "Hooks.h"
#include "HooksDummyObjectCleanupGuard.h"
#include "dsound_proxy.h"
#include "Globals.h"
#include "HookD3D12.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderInjectorGUI.h"
#include "StringHelper.h"
#include "VTableIndex.h"

namespace Hooks
{
	//dummy objects are created only to discover D3D12/DXGI vtable addresses for MinHook.
	static Microsoft::WRL::ComPtr<IDXGISwapChain3> gDummySwapChain = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12Device> gDummyDevice = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12CommandQueue> gDummyCommandQueue = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> gDummyCommandAllocator = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> gDummyCommandList = nullptr;
	static HWND gDummyWindow = nullptr;
	static const wchar_t* gDummyWindowClassName = L"DummyWndClass";

	static LPVOID gPresentTarget = nullptr;
	static LPVOID gPresent1Target = nullptr;
	static LPVOID gResizeBuffersTarget = nullptr;
	static LPVOID gExecuteCommandListsTarget = nullptr;
	static bool gOptiScalerCompatibilityEnabled = false;
	static bool gObjectLocalSwapChainHooksEnabled = false;

} //namespace Hooks

namespace HookD3D12
{
	FunctionCreateSwapChain gOriginalCreateSwapChain = nullptr;
	FunctionCreateSwapChainForHwnd gOriginalCreateSwapChainForHwnd = nullptr;

	void CaptureCreatedSwapChain(IUnknown* creationDevice, IUnknown* swapChain)
	{
		if (!swapChain)
			return;

		Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;

		if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
			return;

		//D3D12 passes the presenting direct command queue as the factory's
		//device argument. Preserve that exact association for overlay work.
		RegisterSwapChainCommandQueue(swapChain3.Get(), creationDevice);

		if (!Hooks::gObjectLocalSwapChainHooksEnabled)
			return;

		//use the active capture layer's name when recording where this swap chain came from.
		const char* compatibilitySource = "RenderDoc";
		if (Hooks::gOptiScalerCompatibilityEnabled)
			compatibilitySource = "OptiScaler";

		if (InstallSwapChainCompatibility(swapChain3.Get(), compatibilitySource))
		{
			ShaderInjectorIO::WriteToLogFile(StringHelper::Format("HookD3D12->CaptureCreatedSwapChain: captured %s swapChain=%p", compatibilitySource, swapChain3.Get()));
		}
	}

} //namespace HookD3D12

namespace Hooks
{
	bool PrepareSwapChainCapture()
	{
		const std::string optiScalerSettingsPath = ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetGameDirectory(), "OptiScaler.ini");
		gOptiScalerCompatibilityEnabled = ShaderInjectorIO::FileExists(optiScalerSettingsPath);
		const bool renderDocCaptureLayerLoaded = GetModuleHandleW(L"renderdoc.dll") != nullptr;
		gObjectLocalSwapChainHooksEnabled = gOptiScalerCompatibilityEnabled || renderDocCaptureLayerLoaded;

		Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
		HRESULT factoryResult = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

		if (FAILED(factoryResult) || !factory)
		{
			ShaderInjectorIO::WriteToLogFileError("Hooks->PrepareSwapChainCapture: failed to create DXGI factory: " + StringHelper::FormatHRESULT(factoryResult));
			return false;
		}

		void** factoryVTable = *reinterpret_cast<void***>(factory.Get());
		void* createSwapChainTarget = factoryVTable[VTableIndex::indexCreateSwapChain];
		void* createSwapChainForHwndTarget = factoryVTable[VTableIndex::indexCreateSwapChainForHwnd];

		MH_STATUS createHwndStatus = MH_CreateHook(createSwapChainForHwndTarget, reinterpret_cast<void*>(&HookD3D12::Hook_CreateSwapChainForHwnd), reinterpret_cast<void**>(&HookD3D12::gOriginalCreateSwapChainForHwnd));
		MH_STATUS enableHwndStatus = createHwndStatus;
		if (createHwndStatus == MH_OK)
			enableHwndStatus = MH_EnableHook(createSwapChainForHwndTarget);

		if (createHwndStatus != MH_OK || (enableHwndStatus != MH_OK && enableHwndStatus != MH_ERROR_ENABLED))
		{
			ShaderInjectorIO::WriteToLogFileError(StringHelper::Format("Hooks->PrepareSwapChainCapture: CreateSwapChainForHwnd hook failed create=%s enable=%s", MH_StatusToString(createHwndStatus), MH_StatusToString(enableHwndStatus)));
			return false;
		}

		MH_STATUS createLegacyStatus = MH_CreateHook(createSwapChainTarget, reinterpret_cast<void*>(&HookD3D12::Hook_CreateSwapChain), reinterpret_cast<void**>(&HookD3D12::gOriginalCreateSwapChain));
		MH_STATUS enableLegacyStatus = createLegacyStatus;
		if (createLegacyStatus == MH_OK)
			enableLegacyStatus = MH_EnableHook(createSwapChainTarget);

		if (createLegacyStatus != MH_OK || (enableLegacyStatus != MH_OK && enableLegacyStatus != MH_ERROR_ENABLED))
		{
			//Rebirth uses CreateSwapChainForHwnd. Keep the capture path active
			//when only the legacy fallback could not be installed.
			ShaderInjectorIO::WriteToLogFileWarning(StringHelper::Format("Hooks->PrepareSwapChainCapture: legacy CreateSwapChain hook unavailable create=%s enable=%s", MH_StatusToString(createLegacyStatus), MH_StatusToString(enableLegacyStatus)));
		}

		if (gOptiScalerCompatibilityEnabled)
		{
			ShaderInjectorIO::WriteToLogFile("Hooks->PrepareSwapChainCapture: enabled exact command-queue capture and OptiScaler object-local swap-chain hooks");
		}
		else if (renderDocCaptureLayerLoaded)
		{
			ShaderInjectorIO::WriteToLogFile("Hooks->PrepareSwapChainCapture: enabled exact command-queue capture and RenderDoc object-local swap-chain hooks");
		}
		else
		{
			ShaderInjectorIO::WriteToLogFile("Hooks->PrepareSwapChainCapture: enabled exact swap-chain command-queue capture");
		}
		return true;
	}

	bool IsOptiScalerCompatibilityEnabled()
	{
		return gOptiScalerCompatibilityEnabled;
	}

	//temporary D3D12 objects expose the vtables used by this machine without touching game objects.
	static HRESULT CreateDeviceAndSwapChain()
	{
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: creating device and swap chain...");

		//the temporary swap chain needs a window, even though the game never sees it.
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: registering dummy window...");

		WNDCLASSEXW windowClass =
			{
				sizeof(WNDCLASSEXW),
				CS_CLASSDC,
				DefWindowProcW,
				0, 0,
				GetModuleHandleW(nullptr),
				nullptr, nullptr, nullptr, nullptr,
				gDummyWindowClassName,
				nullptr};

		if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->CreateDeviceAndSwapChain: RegisterClassExW failed: %lu", static_cast<unsigned long>(GetLastError())));
			return E_FAIL;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: registered dummy window!");
		//keep the discovery window small and hidden while collecting swap-chain methods.
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: creating hidden window...");

		gDummyWindow = CreateWindowExW(
			0, gDummyWindowClassName, L"Dummy",
			WS_OVERLAPPEDWINDOW,
			0, 0, 1, 1,
			nullptr, nullptr, windowClass.hInstance, nullptr);

		if (!gDummyWindow)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->CreateDeviceAndSwapChain: CreateWindowExW failed: %lu", static_cast<unsigned long>(GetLastError())));
			return E_FAIL;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: created hidden window!");
		//create a local DXGI factory so its swap chain has the same interface shape as the game's.
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1...");

		Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory4;
		HRESULT dxgiFactory4Result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory4));

		if (FAILED(dxgiFactory4Result))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1 failed: " + StringHelper::FormatHRESULT(dxgiFactory4Result));
			return dxgiFactory4Result;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1 Success!");
		//the device provides the queue and command list needed to locate their methods.
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice...");

		HRESULT createdD3D12DeviceResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gDummyDevice));

		if (FAILED(createdD3D12DeviceResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice failed: " + StringHelper::FormatHRESULT(createdD3D12DeviceResult));
			return createdD3D12DeviceResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice Success!");
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandQueue...");

		D3D12_COMMAND_QUEUE_DESC commandQueueDescription = {};
		commandQueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		commandQueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		HRESULT createCommandQueueResult = gDummyDevice->CreateCommandQueue(&commandQueueDescription, IID_PPV_ARGS(&gDummyCommandQueue));

		if (FAILED(createCommandQueueResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: CreateCommandQueue failed: " + StringHelper::FormatHRESULT(createCommandQueueResult));
			return createCommandQueueResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandQueue Success!");
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandAllocator...");

		HRESULT createCommandAllocatorResult = gDummyDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&gDummyCommandAllocator));

		if (FAILED(createCommandAllocatorResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: CreateCommandAllocator failed: " + StringHelper::FormatHRESULT(createCommandAllocatorResult));
			return createCommandAllocatorResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandAllocator Success!");
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandList...");

		HRESULT createCommandListResult = gDummyDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			gDummyCommandAllocator.Get(),
			nullptr,
			IID_PPV_ARGS(&gDummyCommandList));

		if (FAILED(createCommandListResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: CreateCommandList failed: " + StringHelper::FormatHRESULT(createCommandListResult));
			return createCommandListResult;
		}

		gDummyCommandList->Close();
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandList Success!");

		if (gObjectLocalSwapChainHooksEnabled)
		{
			//wrapper layers can expose different Present and ResizeBuffers methods on the live swap chain.
			//the factory hook captures that object and installs its private vtable hooks instead.
			ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: object-local swap-chain hooks active; skipping dummy swap chain");
			return S_OK;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateSwapChainForHwnd...");

		DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {};
		swapChainDescription.BufferCount = 2;
		swapChainDescription.Width = 1;
		swapChainDescription.Height = 1;
		swapChainDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDescription.SampleDesc.Count = 1;

		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
		HRESULT createSwapChain1Result = dxgiFactory4->CreateSwapChainForHwnd(
			gDummyCommandQueue.Get(),
			gDummyWindow,
			&swapChainDescription,
			nullptr, nullptr,
			&swapChain1);

		if (FAILED(createSwapChain1Result))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: CreateSwapChainForHwnd failed: " + StringHelper::FormatHRESULT(createSwapChain1Result));
			return createSwapChain1Result;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateSwapChainForHwnd Success!");
		//the overlay needs the IDXGISwapChain3 interface used by the live rendering path.
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: querying IDXGISwapChain3...");

		HRESULT swapChainQueryResult = swapChain1.As(&gDummySwapChain);

		if (FAILED(swapChainQueryResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: QueryInterface IDXGISwapChain3 failed: " + StringHelper::FormatHRESULT(swapChainQueryResult));
			return swapChainQueryResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: QueryInterface IDXGISwapChain3 Success!");
		return S_OK;
	}

	void Initialize()
	{
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->Initialize: initalizing hooks...");

		DummyObjectCleanupGuard dummyObjectCleanupGuard;

		HRESULT createDeviceAndSwapChainResult = CreateDeviceAndSwapChain();

		if (FAILED(createDeviceAndSwapChainResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->Initialize: Failed to create dummy device/swapchain.");
			return;
		}

		//read the queue vtable while its temporary COM object is still alive.

		auto commandQueueVTable = *reinterpret_cast<void***>(gDummyCommandQueue.Get());

		HookD3D12::InstallPipelineHooksForDevice(gDummyDevice.Get());
		HookD3D12::InstallCommandListHooksForCommandList(gDummyCommandList.Get());

		MH_STATUS minHookStatus;

		if (!gObjectLocalSwapChainHooksEnabled)
		{
			auto swapChainVTable = *reinterpret_cast<void***>(gDummySwapChain.Get());

			//process-wide swap-chain hooks are used when private object hooks are unavailable.
			gPresentTarget = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexPresent]);
			minHookStatus = MH_CreateHook(gPresentTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_PresentD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_PresentD3D12));

			if (minHookStatus != MH_OK)
				ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook Present failed: %s", MH_StatusToString(minHookStatus)));

			gPresent1Target = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexPresent1]);
			minHookStatus = MH_CreateHook(gPresent1Target, reinterpret_cast<LPVOID>(HookD3D12::Hook_Present1D3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_Present1D3D12));

			if (minHookStatus != MH_OK)
				ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook Present1 failed: %s", MH_StatusToString(minHookStatus)));

			gResizeBuffersTarget = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexResizeBuffers]);
			minHookStatus = MH_CreateHook(gResizeBuffersTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_ResizeBuffersD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_ResizeBuffersD3D12));

			if (minHookStatus != MH_OK)
				ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook ResizeBuffers failed: %s", MH_StatusToString(minHookStatus)));
		}

		//queue submission tells the render-pass runtime when recorded work reaches the GPU.
		gExecuteCommandListsTarget = reinterpret_cast<LPVOID>(commandQueueVTable[VTableIndex::indexExecuteCommandLists]);
		minHookStatus = MH_CreateHook(gExecuteCommandListsTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_ExecuteCommandListsD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_ExecuteCommandListsD3D12));

		if (minHookStatus != MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook ExecuteCommandLists failed: %s", MH_StatusToString(minHookStatus)));

		//publish the prepared hooks together after every trampoline has been configured.
		minHookStatus = MH_EnableHook(MH_ALL_HOOKS);

		if (minHookStatus != MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_EnableHook failed: %s", MH_StatusToString(minHookStatus)));
		else
			ShaderInjectorGUI::WriteToRuntimeLog("Hooks->Initialize: Hooks enabled.");
	}

	void CleanupDummyObjects()
	{
		//DXGI swap chains retain presentation state associated with their window.
		//release the COM objects before destroying that window for Wine and VKD3D compatibility.
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CleanupDummyObjects: releasing command list...");
		gDummyCommandList.Reset();

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CleanupDummyObjects: releasing command allocator...");
		gDummyCommandAllocator.Reset();

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CleanupDummyObjects: releasing swap chain...");
		gDummySwapChain.Reset();

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CleanupDummyObjects: releasing command queue...");
		gDummyCommandQueue.Reset();

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CleanupDummyObjects: releasing device...");
		gDummyDevice.Reset();

		if (gDummyWindow)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CleanupDummyObjects: destroying window...");
			DestroyWindow(gDummyWindow);
			gDummyWindow = nullptr;
		}

		UnregisterClassW(gDummyWindowClassName, GetModuleHandleW(nullptr));
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CleanupDummyObjects: complete.");
	}
} //namespace Hooks
