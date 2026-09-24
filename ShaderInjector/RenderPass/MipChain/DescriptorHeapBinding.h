#pragma once

#include <d3d12.h>

namespace RenderPassMipChain
{
	//remember a game's descriptor heap bounds before mip generation changes bindings.
	struct DescriptorHeapBinding
	{
		ID3D12DescriptorHeap* heap = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		UINT descriptorCount = 0;
		UINT descriptorIncrementSize = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
	};
} //namespace RenderPassMipChain
