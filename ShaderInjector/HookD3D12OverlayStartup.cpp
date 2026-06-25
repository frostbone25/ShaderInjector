#include "PreCompiledHeader.h"

#include "HookD3D12OverlayStartup.h"
#include "ShaderInjectorGUI.h"

#include <d3d12.h>

namespace HookD3D12
{
	namespace
	{
		struct OverlayStartupGateState
		{
			HWND outputWindow = nullptr;
			UINT bufferCount = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			UINT flags = 0;
			LONG clientWidth = 0;
			LONG clientHeight = 0;
			int stableFrames = 0;
			ULONGLONG firstStableTick = 0;
		};

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

		HWND outputWindow = outDesc.OutputWindow;
		if (!outputWindow || !IsWindow(outputWindow) || IsIconic(outputWindow))
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
		if (!GetClientRect(outputWindow, &clientRect))
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
			gOverlayStartupGate.outputWindow != outputWindow ||
			gOverlayStartupGate.bufferCount != outDesc.BufferCount ||
			gOverlayStartupGate.format != outDesc.BufferDesc.Format ||
			gOverlayStartupGate.flags != outDesc.Flags ||
			gOverlayStartupGate.clientWidth != clientWidth ||
			gOverlayStartupGate.clientHeight != clientHeight;

		if (swapChainChanged)
		{
			gOverlayStartupGate.outputWindow = outputWindow;
			gOverlayStartupGate.bufferCount = outDesc.BufferCount;
			gOverlayStartupGate.format = outDesc.BufferDesc.Format;
			gOverlayStartupGate.flags = outDesc.Flags;
			gOverlayStartupGate.clientWidth = clientWidth;
			gOverlayStartupGate.clientHeight = clientHeight;
			gOverlayStartupGate.stableFrames = 1;
			gOverlayStartupGate.firstStableTick = now;
			return false;
		}

		if (!ProbeSwapChainBuffers(swapChain, outDesc.BufferCount))
		{
			gOverlayStartupGate.stableFrames = 0;
			gOverlayStartupGate.firstStableTick = now;
			return false;
		}

		++gOverlayStartupGate.stableFrames;
		return gOverlayStartupGate.stableFrames >= kOverlayStartupStableFrameLimit &&
			(now - gOverlayStartupGate.firstStableTick) >= kOverlayStartupMinimumStableMs;
	}

	void NotifyOverlayResizeBuffersSucceeded()
	{
		gLastResizeBuffersTick = GetTickCount64();
		gLoggedResizeCooldown = false;
		ResetOverlayStartupGate();
	}
}
