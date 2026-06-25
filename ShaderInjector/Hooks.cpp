#include "Hooks.h"
#include "PreCompiledHeader.h"
#include <windows.h>
#include "dsound_proxy.h"
#include "MinHook.h"
#include <cstdio>
#include "Globals.h"
#include "HookD3D12.h"
#include <dxgi1_4.h>
#include "wrl/client.h"
#include <dxgi.h>
#include <d3d12.h>
#include "ShaderInjectorIO.h"
#include "ShaderInjectorGUI.h"
#include "VTableIndex.h"

namespace Hooks
{
	//dummy objects for v tables
	static Microsoft::WRL::ComPtr<IDXGISwapChain3>           pSwapChain = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12Device>              pDevice = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12CommandQueue>        pCommandQueue = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    pCommandAllocator = nullptr;
	static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pCommandList = nullptr;
	static HWND hDummyWindow = nullptr;
	static const wchar_t* dummyClassName = L"DummyWndClass";

	static LPVOID pPresentTarget = nullptr;
	static LPVOID pPresent1Target = nullptr;
	static LPVOID pResizeBuffersTarget = nullptr;
	static LPVOID pExecuteCommandListsTarget = nullptr;

	static void CleanupDummyObjects()
	{
		if (hDummyWindow)
		{
			DestroyWindow(hDummyWindow);
			hDummyWindow = nullptr;
		}

		UnregisterClassW(dummyClassName, GetModuleHandle(nullptr));

		pCommandList.Reset();
		pCommandAllocator.Reset();
		pSwapChain.Reset();
		pCommandQueue.Reset();
		pDevice.Reset();
	}

	// Create hidden Window + device + DX12 swapchain
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
			dummyClassName,
			nullptr
		};

		if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		{
			//NOTE: keep this comment around for sanity check please!
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: RegisterClassExW failed: %u\n", GetLastError());
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return E_FAIL;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: registered dummy window!");
		//============================== 2) Create hidden window ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: creating hidden window...");

		hDummyWindow = CreateWindowExW(
			0, dummyClassName, L"Dummy",
			WS_OVERLAPPEDWINDOW,
			0, 0, 1, 1,
			nullptr, nullptr, windowClass.hInstance, nullptr
		);

		if (!hDummyWindow) 
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: CreateWindowExW failed: %u\n", GetLastError());
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return E_FAIL;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: created hidden window!");
		//============================== 3) Factory DXGI ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1...");

		Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory4;
		HRESULT dxgiFactory4Result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory4));

		if (FAILED(dxgiFactory4Result))
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1 failed: 0x%08X\n", dxgiFactory4Result);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return dxgiFactory4Result;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateDXGIFactory1 Success!");
		//============================== 4) Device D3D12 ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice...");

		HRESULT createdD3D12DeviceResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));

		if (FAILED(createdD3D12DeviceResult))
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice failed: 0x%08X\n", createdD3D12DeviceResult);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return createdD3D12DeviceResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: D3D12CreateDevice Success!");
		//============================== 5) Command Queue ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandQueue...");

		D3D12_COMMAND_QUEUE_DESC commandQueueDescription = {};
		commandQueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		commandQueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		HRESULT createCommandQueueResult = pDevice->CreateCommandQueue(&commandQueueDescription, IID_PPV_ARGS(&pCommandQueue));

		if (FAILED(createCommandQueueResult))
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: CreateCommandQueue failed: 0x%08X\n", createCommandQueueResult);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return createCommandQueueResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandQueue Success!");
		//============================== 6) Command List ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandAllocator...");

		HRESULT createCommandAllocatorResult = pDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&pCommandAllocator));

		if (FAILED(createCommandAllocatorResult))
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: CreateCommandAllocator failed: 0x%08X\n", createCommandAllocatorResult);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return createCommandAllocatorResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandAllocator Success!");
		//============================== 7) Command List ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateCommandList...");

		HRESULT createCommandListResult = pDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			pCommandAllocator.Get(),
			nullptr,
			IID_PPV_ARGS(&pCommandList));

		if (FAILED(createCommandListResult))
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: CreateCommandList failed: 0x%08X\n", createCommandListResult);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return createCommandListResult;
		}

		pCommandList->Close();
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
			pCommandQueue.Get(),
			hDummyWindow,
			&swapChainDescription,
			nullptr, nullptr,
			&swapChain1
		);

		if (FAILED(createSwapChain1Result))
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: CreateSwapChainForHwnd failed: 0x%08X\n", createSwapChain1Result);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return createSwapChain1Result;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: CreateSwapChainForHwnd Success!");
		//============================== 9) Query IDXGISwapChain3 ==============================
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: querying IDXGISwapChain3...");

		HRESULT swapChainQueryResult = swapChain1.As(&pSwapChain);

		if (FAILED(swapChainQueryResult))
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->CreateDeviceAndSwapChain: QueryInterface IDXGISwapChain3 failed: 0x%08X\n", swapChainQueryResult);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			return swapChainQueryResult;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->CreateDeviceAndSwapChain: QueryInterface IDXGISwapChain3 Success!");
		return S_OK;
	}

	void Initalize() 
	{
		//IMPORTANT NOTE 1: We are able to get to this point and call this function
		//NOTE: keep this comment around for sanity check please!
		ShaderInjectorGUI::WriteToRuntimeLog("Hooks->Initalize: initalizing hooks...");

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
			ShaderInjectorGUI::WriteToRuntimeLogError("Hooks->Initalize: Failed to create dummy device/swapchain.");
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

		auto swapChainVTable = *reinterpret_cast<void***>(pSwapChain.Get());
		auto commandQueueVTable = *reinterpret_cast<void***>(pCommandQueue.Get());

		HookD3D12::InstallPipelineHooksForDevice(pDevice.Get());
		HookD3D12::InstallCommandListHooksForCommandList(pCommandList.Get());

		//======================================== Hook Start ========================================

		MH_STATUS minHookStatus;

		//======================================== Hook_PresentD3D12 ========================================
		pPresentTarget = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexPresent]);
		minHookStatus = MH_CreateHook(pPresentTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_PresentD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_PresentD3D12));
		
		if (minHookStatus != MH_OK)
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->Initalize: MH_CreateHook Present failed: %s\n", MH_StatusToString(minHookStatus));
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
		}

		//======================================== Hook_Present1D3D12 ========================================
		pPresent1Target = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexPresent1]);
		minHookStatus = MH_CreateHook(pPresent1Target, reinterpret_cast<LPVOID>(HookD3D12::Hook_Present1D3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_Present1D3D12));

		if (minHookStatus != MH_OK)
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->Initalize: MH_CreateHook Present1 failed: %s\n", MH_StatusToString(minHookStatus));
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
		}

		//======================================== Hook_ResizeBuffersD3D12 ========================================
		pResizeBuffersTarget = reinterpret_cast<LPVOID>(swapChainVTable[VTableIndex::indexResizeBuffers]);
		minHookStatus = MH_CreateHook(pResizeBuffersTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_ResizeBuffersD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_ResizeBuffersD3D12));

		if (minHookStatus != MH_OK)
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->Initalize: MH_CreateHook ResizeBuffers failed: %s\n", MH_StatusToString(minHookStatus));
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
		}

		//======================================== Hook_ExecuteCommandListsD3D12 ========================================
		pExecuteCommandListsTarget = reinterpret_cast<LPVOID>(commandQueueVTable[VTableIndex::indexExecuteCommandLists]);
		minHookStatus = MH_CreateHook(pExecuteCommandListsTarget, reinterpret_cast<LPVOID>(HookD3D12::Hook_ExecuteCommandListsD3D12), reinterpret_cast<LPVOID*>(&HookD3D12::Original_ExecuteCommandListsD3D12));

		if (minHookStatus != MH_OK)
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->Initalize: MH_CreateHook ExecuteCommandLists failed: %s\n", MH_StatusToString(minHookStatus));
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
		}

		//======================================== Enable Hooks ========================================
		//enable all hooks ---
		minHookStatus = MH_EnableHook(MH_ALL_HOOKS);

		if (minHookStatus != MH_OK)
		{
			char buffer[256];
			sprintf_s(buffer, "Hooks->Initalize: MH_EnableHook failed: %s\n", MH_StatusToString(minHookStatus));
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
		}
		else
		{
			ShaderInjectorGUI::WriteToRuntimeLog("Hooks->Initalize: Hooks enabled.");
			/*
			char buffer[256];
			sprintf_s(buffer, "Hooks | Initalize | Hooks enabled. Present@%p (idx=%zu), Present1@%p (idx=%zu), Resize@%p (idx=%zu), Exec@%p (idx=%zu)\n",
				reinterpret_cast<LPVOID>(scVTable[kPresentIndex]), kPresentIndex,
				reinterpret_cast<LPVOID>(scVTable[kPresent1Index]), kPresent1Index,
				reinterpret_cast<LPVOID>(scVTable[kResizeBuffersIndex]), kResizeBuffersIndex,
				reinterpret_cast<LPVOID>(cqVTable[kExecuteCommandListsIndex]), kExecuteCommandListsIndex);
			MessageBoxA(nullptr, buffer, "Shader Injector", MB_OK);
			*/
		}
	}
}
