#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>

namespace HookD3D12
{
	struct SwapChainCommandQueueBinding
	{
		IDXGISwapChain3* swapChain = nullptr;
		ID3D12CommandQueue* commandQueue = nullptr;
	};
} //namespace HookD3D12
