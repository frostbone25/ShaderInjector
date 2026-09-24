#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace RenderPassTexturePool
{
	//assign increasing fence values to texture batches submitted on one queue.
	struct QueueFence
	{
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		UINT64 nextValue = 0;
	};
} //namespace RenderPassTexturePool
