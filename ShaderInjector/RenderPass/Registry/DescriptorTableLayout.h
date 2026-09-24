#pragma once

#include <cstdint>

#include <d3d12.h>

namespace RenderPassResourceRegistry
{
	//describe the root table size and visibility used when copying descriptors.
	struct DescriptorTableLayout
	{
		UINT rootParameterIndex = UINT32_MAX;
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		UINT descriptorCount = 0;
		bool containsUnboundedRange = false;
	};
} //namespace RenderPassResourceRegistry
