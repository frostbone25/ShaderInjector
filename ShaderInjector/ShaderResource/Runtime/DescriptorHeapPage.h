#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace ShaderResourceRuntime
{
	//allocate descriptor copies from pages so a command list can grow in chunks.
	struct DescriptorHeapPage
	{
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		UINT capacity = 0;
		UINT usedDescriptors = 0;
	};
} //namespace ShaderResourceRuntime
