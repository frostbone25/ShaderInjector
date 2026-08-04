#pragma once

#include <windows.h>
#include <dxgi1_4.h>

namespace HookD3D12
{
	void ResetOverlayStartupGate();
	bool IsSwapChainReadyForOverlayInitialization(IDXGISwapChain3* swapChain, DXGI_SWAP_CHAIN_DESC& outDesc);
	void NotifyOverlayResizeBuffersSucceeded();
}
