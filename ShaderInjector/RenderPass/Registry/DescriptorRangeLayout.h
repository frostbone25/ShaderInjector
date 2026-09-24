#pragma once

#include <d3d12.h>

namespace RenderPassResourceRegistry
{
	//describe one serialized root-signature range before resolving descriptor locations.
	struct DescriptorRangeLayout
	{
		D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		UINT descriptorCount = 0;
		UINT baseShaderRegister = 0;
		UINT registerSpace = 0;
		UINT tableOffset = 0;
	};
} //namespace RenderPassResourceRegistry
