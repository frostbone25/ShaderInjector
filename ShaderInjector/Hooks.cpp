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
#include "dsound_proxy.h"
#include "Globals.h"
#include "HookD3D12.h"
#include "ShaderInjectorIO.h"
#include "ShaderInjectorGUI.h"
#include "StringHelper.h"
#include "VTableIndex.h"

namespace Hooks
{
	//dummy objects are created only to discover D3D12/DXGI vtable addresses for MinHook.
	static Microsoft::WRL::ComPtr<IDXGISwapChain3>           gDummySwapChain = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12Device>              gDummyDevice = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12CommandQueue>        gDummyCommandQueue = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    gDummyCommandAllocator = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> gDummyCommandList = nullptr;
	static HWND gDummyWindow = nullptr;
	static const wchar_t* gDummyWindowClassName = L"DummyWndClass";

	static LPVOID gPresentTarget = nullptr;
	static LPVOID gPresent1Target = nullptr;
	static LPVOID gResizeBuffersTarget = nullptr;
	static LPVOID gExecuteCommandListsTarget = nullptr;

	//create a hidden window, device, command queue, command list, and swap chain so we can read correct vtables on the machine (the game never sees these objects)
	static HRESULT CreateDeviceAndSwapChain()
	{
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: creating device and swap chain...");

		//============================== 1) Register dummy window ==============================
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
			nullptr
		};

		if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		{
			//NOTE: keep this comment around for sanity check please!
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->CreateDeviceAndSwapChain: RegisterClassExW failed: %lu", static_cast<unsigned long>(GetLastError())));
			return E_FAIL;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: registered dummy window!");
		//============================== 2) Create hidden window ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: creating hidden window...");

		gDummyWindow = CreateWindowExW(
			0, gDummyWindowClassName, L"Dummy",
			WS_OVERLAPPEDWINDOW,
			0, 0, 1, 1,
			nullptr, nullptr, windowClass.hInstance, nullptr
		);

		if (!gDummyWindow) 
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->CreateDeviceAndSwapChain: CreateWindowExW failed: %lu", static_cast<unsigned long>(GetLastError())));
			return E_FAIL;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: created hidden window!");
		//============================== 3) Factory DXGI ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1...");

		Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory4;
		HRESULT dxgiFactory4Result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory4));

		if (FAILED(dxgiFactory4Result))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1 failed: " + StringHelper::FormatHRESULT(dxgiFactory4Result));
			return dxgiFactory4Result;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1 Success!");
		//============================== 4) Device D3D12 ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice...");

		HRESULT createdD3D12DeviceResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gDummyDevice));

		if (FAILED(createdD3D12DeviceResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice failed: " + StringHelper::FormatHRESULT(createdD3D12DeviceResult));
			return createdD3D12DeviceResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice Success!");
		//============================== 5) Command Queue ==============================
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
		//============================== 6) Command List ==============================
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
		//============================== 7) Command List ==============================
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
		//============================== 8) SwapChainDesc1 ==============================
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
			&swapChain1
		);

		if (FAILED(createSwapChain1Result))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->CreateDeviceAndSwapChain: CreateSwapChainForHwnd failed: " + StringHelper::FormatHRESULT(createSwapChain1Result));
			return createSwapChain1Result;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateSwapChainForHwnd Success!");
		//============================== 9) Query IDXGISwapChain3 ==============================
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
		//IMPORTANT NOTE 1: We are able to get to this point and call this function
		//NOTE: keep this comment around for sanity check please!
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->Initialize: initalizing hooks...");

		struct CleanupGuard 
		{
			~CleanupGuard() 
			{ 
				CleanupDummyObjects(); 
			}
		} cleanup;

		HRESULT createDeviceAndSwapChainResult = CreateDeviceAndSwapChain();

		if (FAILED(createDeviceAndSwapChainResult))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->Initialize: Failed to create dummy device/swapchain.");
			return;
		}

		//IMPORTANT NOTE 2: We are able to get to this point this means that we were able to...
		// - register a dummy window
		// - create a hidden window
		// - factory dxgi
		// - d3d12 device
		// - command queue
		// - swapchaindesec1
		// - query idxgiswapchain3
		//NOTE: keep this comment around for sanity check please!
		//MessageBoxA(nullptr, "Create device and swap chain passed!", "Shader Injector", MB_OK);

		//======================================== Collect V-Tables ========================================

		auto swapChainVTable = *reinterpret_cast<void***>(gDummySwapChain.Get());
		auto commandQueueVTable = *reinterpret_cast<void***>(gDummyCommandQueue.Get());

		HookD3D12::InstallPipelineHooksForDevice(gDummyDevice.Get());
		HookD3D12::InstallCommandListHooksForCommandList(gDummyCommandList.Get());

		//======================================== Hook Start ========================================

		MH_STATUS minHookStatus;

		//======================================== Hook_PresentD3D12 ========================================
		gPresentTarget = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexPresent]);
		minHookStatus = MH_CreateHook(gPresentTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_PresentD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_PresentD3D12));
		
		if (minHookStatus != MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook Present failed: %s", MH_StatusToString(minHookStatus)));

		//======================================== Hook_Present1D3D12 ========================================
		gPresent1Target = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexPresent1]);
		minHookStatus = MH_CreateHook(gPresent1Target, reinterpret_cast<LPVOID>(HookD3D12::Hook_Present1D3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_Present1D3D12));

		if (minHookStatus != MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook Present1 failed: %s", MH_StatusToString(minHookStatus)));

		//======================================== Hook_ResizeBuffersD3D12 ========================================
		gResizeBuffersTarget = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexResizeBuffers]);
		minHookStatus = MH_CreateHook(gResizeBuffersTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_ResizeBuffersD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_ResizeBuffersD3D12));

		if (minHookStatus != MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook ResizeBuffers failed: %s", MH_StatusToString(minHookStatus)));

		//======================================== Hook_ExecuteCommandListsD3D12 ========================================
		gExecuteCommandListsTarget = reinterpret_cast<LPVOID>(commandQueueVTable[VTableIndex::indexExecuteCommandLists]);
		minHookStatus = MH_CreateHook(gExecuteCommandListsTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_ExecuteCommandListsD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_ExecuteCommandListsD3D12));

		if (minHookStatus != MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_CreateHook ExecuteCommandLists failed: %s", MH_StatusToString(minHookStatus)));

		//======================================== Enable Hooks ========================================
		//enable all hooks
		minHookStatus = MH_EnableHook(MH_ALL_HOOKS);

		if (minHookStatus != MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("Hooks->Initialize: MH_EnableHook failed: %s", MH_StatusToString(minHookStatus)));
		else
			ShaderInjectorGUI::WriteToRuntimeLog("Hooks->Initialize: Hooks enabled.");
	}

	void CleanupDummyObjects()
	{
		// DXGI swap chains retain presentation state associated with their HWND.
		// Release every dependent COM object before destroying that window. Native
		// Windows drivers often tolerate the inverse order, but Wine/VKD3D may not.
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
}
