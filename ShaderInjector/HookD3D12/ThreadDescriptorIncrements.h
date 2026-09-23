#pragma once

#include <array>

#include <d3d12.h>

namespace HookD3D12
{
	struct ThreadDescriptorIncrements
	{
		ID3D12Device* device = nullptr;
		std::array<UINT, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> descriptorIncrementSizes{};
	};
}
