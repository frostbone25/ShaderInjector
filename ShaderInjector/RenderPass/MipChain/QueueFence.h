#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace RenderPassMipChain
{
	//assign fence values to mip-generation work submitted on a command queue.
	struct QueueFence
	{
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		UINT64 nextValue = 0;
	};
} //namespace RenderPassMipChain
