#pragma once

#include <cstdint>

#include <d3d12.h>

namespace RenderPassMipChain
{
	//map a game descriptor table into a temporary heap used by mip generation.
	struct ActiveDescriptorTable
	{
		UINT rootParameterIndex = UINT32_MAX;
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		UINT descriptorCount = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE originalGpuHandle{};
		D3D12_CPU_DESCRIPTOR_HANDLE originalCpuHandle{};
		UINT customHeapOffset = 0;
	};
} //namespace RenderPassMipChain
