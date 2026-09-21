#pragma once

#include <dxgi.h>
#include <string>

namespace HookD3D12
{
	struct D3D12PipelineInfo
	{
		std::string gpuName;

		UINT vendorId = 0;
		UINT deviceId = 0;

		SIZE_T dedicatedVideoMemory = 0;
		SIZE_T dedicatedSystemMemory = 0;
		SIZE_T sharedSystemMemory = 0;

		UINT resourceBindingTier = 0;
		UINT tiledResourcesTier = 0;
		UINT conservativeRasterTier = 0;
		UINT raytracingTier = 0;
		UINT meshShaderTier = 0;

		UINT swapChainBuffers = 0;
		DXGI_FORMAT swapChainFormat = DXGI_FORMAT_UNKNOWN;

		UINT commandQueueType = 0;
	};
}
