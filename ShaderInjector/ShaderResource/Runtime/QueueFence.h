#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace ShaderResourceRuntime
{
	//give each resource submission on a queue a fence value for retirement.
	struct QueueFence
	{
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		UINT64 nextValue = 0;
	};
} //namespace ShaderResourceRuntime
