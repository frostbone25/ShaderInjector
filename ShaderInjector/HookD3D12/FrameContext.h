#pragma once

#include <d3d12.h>

namespace HookD3D12
{
	struct FrameContext
	{
		ID3D12CommandAllocator* allocator = nullptr;
		ID3D12Resource* renderTarget = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
		UINT64 fenceValue = 0;
	};
}
