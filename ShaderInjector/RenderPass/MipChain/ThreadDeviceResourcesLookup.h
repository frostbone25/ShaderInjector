#pragma once

#include <d3d12.h>

#include "RenderPass/MipChain/DeviceResources.h"

namespace RenderPassMipChain
{
	//avoid a shared map lookup when the thread keeps using the same device.
	struct ThreadDeviceResourcesLookup
	{
		ID3D12Device* device = nullptr;
		DeviceResources* resources = nullptr;
	};
} //namespace RenderPassMipChain
