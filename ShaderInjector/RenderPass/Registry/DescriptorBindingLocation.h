#pragma once

#include <cstdint>

#include <d3d12.h>

namespace RenderPassResourceRegistry
{
	//point to the table slot that matches a shader register and resource view.
	struct DescriptorBindingLocation
	{
		UINT rootParameterIndex = UINT32_MAX;
		UINT tableOffset = UINT32_MAX;
		UINT shaderRegister = UINT32_MAX;
		UINT registerSpace = UINT32_MAX;
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		UINT descriptorCount = 0;
		bool tableContainsUnboundedRange = false;
	};
} //namespace RenderPassResourceRegistry
