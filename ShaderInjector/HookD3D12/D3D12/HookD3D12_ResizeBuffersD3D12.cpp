#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"
#include "../HookD3D12OverlayStartup.h"
#include "GUI/ShaderInjectorGUI.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace HookD3D12
{
	bool WaitForOverlayGPUIdle(DWORD timeoutMs)
	{
		if (!gCommandQueue || !gOverlayFence || !gFenceEvent)
			return true;

		++gOverlayFenceValue;

		HRESULT hr = gCommandQueue->Signal(gOverlayFence, gOverlayFenceValue);

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->WaitForOverlayGPUIdle: Overlay fence signal failed during resize teardown.");
			return false;
		}

		if (gOverlayFence->GetCompletedValue() >= gOverlayFenceValue)
			return true;

		hr = gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent);

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->WaitForOverlayGPUIdle: Overlay fence SetEventOnCompletion failed during resize teardown.");
			return false;
		}

		DWORD waitResult = WaitForSingleObject(gFenceEvent, timeoutMs);

		if (waitResult != WAIT_OBJECT_0)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->WaitForOverlayGPUIdle: Overlay fence wait timed out during resize teardown; releasing overlay resources anyway.");
			return false;
		}

		return true;
	}

	static void ShutdownImGuiOverlayBackend()
	{
		gOverlayDeviceObjectsCreated = false;

		if (!ImGui::GetCurrentContext())
			return;

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void ReleaseOverlaySwapChainResources(bool shutdownImGuiBackend)
	{
		gInitialized = false;

		if (shutdownImGuiBackend)
			ShutdownImGuiOverlayBackend();

		if (gCommandList)
		{
			gCommandList->Release();
			gCommandList = nullptr;
		}

		if (gFrameContexts)
		{
			for (UINT i = 0; i < gBufferCount; ++i)
			{
				if (gFrameContexts[i].renderTarget)
				{
					gFrameContexts[i].renderTarget->Release();
					gFrameContexts[i].renderTarget = nullptr;
				}

				if (gFrameContexts[i].allocator)
				{
					gFrameContexts[i].allocator->Release();
					gFrameContexts[i].allocator = nullptr;
				}
			}

			delete[] gFrameContexts;
			gFrameContexts = nullptr;
		}

		if (gHeapRTV)
		{
			gHeapRTV->Release();
			gHeapRTV = nullptr;
		}

		if (gHeapSRV)
		{
			gHeapSRV->Release();
			gHeapSRV = nullptr;
		}

		gBufferCount = 0;
		gInitialized = false;
	}

	static HRESULT HandleResizeBuffersD3D12(
		IDXGISwapChain3* pSwapChain,
		UINT BufferCount,
		UINT Width,
		UINT Height,
		DXGI_FORMAT NewFormat,
		UINT SwapChainFlags,
		FunctionResizeBuffersD3D12 resizeBuffersOverride = nullptr)
	{
		char buffer[1024];
		sprintf_s(buffer, "HookD3D12->Hook_ResizeBuffersD3D12: ResizeBuffers %ux%u Buffers=%u", Width, Height, BufferCount);
		ShaderInjectorGUI::WriteToRuntimeLog(buffer);

		// Release every overlay object that depends on swap-chain buffers or descriptor heaps before ResizeBuffers.
		// ImGui owns a font texture/SRV through the DX12 backend, so a partial back-buffer release can corrupt the UI after resize.
		gOverlayRenderingDisabled = true;

		if (gInitialized || gFrameContexts || gHeapRTV || gHeapSRV || gCommandList)
		{
			WaitForOverlayGPUIdle();
			ReleaseOverlaySwapChainResources(true);
		}

		FunctionResizeBuffersD3D12 resizeBuffers = resizeBuffersOverride
			? resizeBuffersOverride
			: Original_ResizeBuffersD3D12;
		HRESULT hr = resizeBuffers
			? resizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags)
			: E_POINTER;

		if (FAILED(hr))
		{
			sprintf_s(buffer, "HookD3D12->Hook_ResizeBuffersD3D12: ResizeBuffers FAILED hr=0x%08X", (UINT)hr);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			gOverlayRenderingDisabled = false;
			ResetOverlayStartupGate();

			return hr;
		}

		//
		// Force reinitialization next Present().
		//
		gBufferCount = 0;
		gInitialized = false;
		gOverlayRenderingDisabled = false;
		NotifyOverlayResizeBuffersSucceeded();
		InstallRTSSSwapChainCompatibility(pSwapChain);

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->Hook_ResizeBuffersD3D12: ResizeBuffers succeeded. Overlay recreation deferred until resize settles.");

		return hr;
	}

	HRESULT STDMETHODCALLTYPE Handle_ResizeBuffersD3D12(
		IDXGISwapChain3* pSwapChain,
		UINT BufferCount,
		UINT Width,
		UINT Height,
		DXGI_FORMAT NewFormat,
		UINT SwapChainFlags)
	{
		if (gInsideSwapChainCompatibilityCall && Original_ResizeBuffersD3D12)
			return Original_ResizeBuffersD3D12(
				pSwapChain,
				BufferCount,
				Width,
				Height,
				NewFormat,
				SwapChainFlags);

		if (!gRuntimeReady.load(std::memory_order_acquire))
			return Original_ResizeBuffersD3D12
				? Original_ResizeBuffersD3D12(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags)
				: E_POINTER;

		return HandleResizeBuffersD3D12(
			pSwapChain,
			BufferCount,
			Width,
			Height,
			NewFormat,
			SwapChainFlags);
	}

	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityResizeBuffers(
		IDXGISwapChain3* pSwapChain,
		UINT BufferCount,
		UINT Width,
		UINT Height,
		DXGI_FORMAT NewFormat,
		UINT SwapChainFlags)
	{
		FunctionResizeBuffersD3D12 downstreamResizeBuffers = nullptr;
		{
			std::lock_guard<std::mutex> lock(gRTSSCompatibilityMutex);
			downstreamResizeBuffers = gRTSSOriginalResizeBuffers;
		}

		if (!downstreamResizeBuffers)
			return Original_ResizeBuffersD3D12
				? Original_ResizeBuffersD3D12(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags)
				: E_POINTER;

		if (!gRuntimeReady.load(std::memory_order_acquire))
		{
			HRESULT result = downstreamResizeBuffers(
				pSwapChain,
				BufferCount,
				Width,
				Height,
				NewFormat,
				SwapChainFlags);

			if (SUCCEEDED(result))
				InstallSwapChainCompatibility(pSwapChain, "External resize");

			return result;
		}

		ScopedSwapChainCompatibilityCall compatibilityScope;
		HRESULT result = HandleResizeBuffersD3D12(
			pSwapChain,
			BufferCount,
			Width,
			Height,
			NewFormat,
			SwapChainFlags,
			downstreamResizeBuffers);

		if (SUCCEEDED(result))
			InstallSwapChainCompatibility(pSwapChain, "External resize");

		return result;
	}

	HRESULT STDMETHODCALLTYPE Hook_ResizeBuffersD3D12(IDXGISwapChain3* swapChain, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
	{
		return Handle_ResizeBuffersD3D12(swapChain, bufferCount, width, height, format, flags);
	}

	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityResizeBuffers(IDXGISwapChain3* swapChain, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
	{
		return Handle_RTSSCompatibilityResizeBuffers(swapChain, bufferCount, width, height, format, flags);
	}
}
