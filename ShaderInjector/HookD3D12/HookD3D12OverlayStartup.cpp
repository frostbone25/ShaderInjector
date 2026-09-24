//HookD3D12OverlayStartup.cpp
#include <d3d12.h>

//custom
#include "HookD3D12.h"
#include "OverlayStartupGateState.h"
#include "ShaderInjectorGUI.h"

namespace HookD3D12
{
	OverlayStartupGateState gOverlayStartupGate;
	ULONGLONG gLastResizeBuffersTick = 0;
	bool gLoggedResizeCooldown = false;

	constexpr int kOverlayStartupStableFrameLimit = 5;
	constexpr ULONGLONG kOverlayStartupMinimumStableMs = 500;
	constexpr ULONGLONG kOverlayResizeCooldownMs = 2500;

	bool ProbeSwapChainBuffers(IDXGISwapChain3* swapChain, UINT bufferCount)
	{
		if (!swapChain || bufferCount == 0)
			return false;

		for (UINT i = 0; i < bufferCount; ++i)
		{
			ID3D12Resource* backBuffer = nullptr;
			HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

			if (backBuffer)
				backBuffer->Release();

			if (FAILED(hr))
				return false;
		}

		return true;
	}

	void ResetOverlayStartupGate()
	{
		gOverlayStartupGate = {};
	}

	bool IsSwapChainReadyForOverlayInitialization(IDXGISwapChain3* swapChain, DXGI_SWAP_CHAIN_DESC& outDesc)
	{
		outDesc = {};

		if (!swapChain)
		{
			ResetOverlayStartupGate();
			return false;
		}

		HRESULT hr = swapChain->GetDesc(&outDesc);

		if (FAILED(hr))
		{
			ResetOverlayStartupGate();
			return false;
		}

		HWND outputWindowHandle = outDesc.OutputWindow;

		if (!outputWindowHandle || !IsWindow(outputWindowHandle) || IsIconic(outputWindowHandle))
		{
			ResetOverlayStartupGate();
			return false;
		}

		if (outDesc.BufferCount == 0 || outDesc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			ResetOverlayStartupGate();
			return false;
		}

		RECT clientRect{};

		if (!GetClientRect(outputWindowHandle, &clientRect))
		{
			ResetOverlayStartupGate();
			return false;
		}

		LONG clientWidth = clientRect.right - clientRect.left;
		LONG clientHeight = clientRect.bottom - clientRect.top;

		if (clientWidth <= 0 || clientHeight <= 0)
		{
			ResetOverlayStartupGate();
			return false;
		}

		ULONGLONG now = GetTickCount64();

		if (gLastResizeBuffersTick != 0 && now - gLastResizeBuffersTick < kOverlayResizeCooldownMs)
		{
			if (!gLoggedResizeCooldown)
			{
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12OverlayStartup->IsSwapChainReadyForOverlayInitialization: Resize cooldown active; delaying overlay recreation.");
				gLoggedResizeCooldown = true;
			}

			return false;
		}

		gLoggedResizeCooldown = false;

		bool swapChainChanged =
			gOverlayStartupGate.outputWindowHandle != outputWindowHandle ||
			gOverlayStartupGate.bufferCount != outDesc.BufferCount ||
			gOverlayStartupGate.swapChainFormat != outDesc.BufferDesc.Format ||
			gOverlayStartupGate.swapChainFlags != outDesc.Flags ||
			gOverlayStartupGate.clientWidth != clientWidth ||
			gOverlayStartupGate.clientHeight != clientHeight;

		if (swapChainChanged)
		{
			gOverlayStartupGate.outputWindowHandle = outputWindowHandle;
			gOverlayStartupGate.bufferCount = outDesc.BufferCount;
			gOverlayStartupGate.swapChainFormat = outDesc.BufferDesc.Format;
			gOverlayStartupGate.swapChainFlags = outDesc.Flags;
			gOverlayStartupGate.clientWidth = clientWidth;
			gOverlayStartupGate.clientHeight = clientHeight;
			gOverlayStartupGate.stableFrameCount = 1;
			gOverlayStartupGate.firstStableTick = now;
			return false;
		}

		if (!ProbeSwapChainBuffers(swapChain, outDesc.BufferCount))
		{
			gOverlayStartupGate.stableFrameCount = 0;
			gOverlayStartupGate.firstStableTick = now;
			return false;
		}

		++gOverlayStartupGate.stableFrameCount;
		return gOverlayStartupGate.stableFrameCount >= kOverlayStartupStableFrameLimit &&
			   (now - gOverlayStartupGate.firstStableTick) >= kOverlayStartupMinimumStableMs;
	}

	void NotifyOverlayResizeBuffersSucceeded()
	{
		gLastResizeBuffersTick = GetTickCount64();
		gLoggedResizeCooldown = false;
		ResetOverlayStartupGate();
	}
} //namespace HookD3D12
