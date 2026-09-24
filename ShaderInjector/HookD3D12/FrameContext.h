#pragma once

#include <d3d12.h>

namespace HookD3D12
{
	struct FrameContext
	{
		ID3D12CommandAllocator* commandAllocator = nullptr;
		ID3D12Resource* renderTargetResource = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = {};
		UINT64 fenceValue = 0;
	};
} //namespace HookD3D12
