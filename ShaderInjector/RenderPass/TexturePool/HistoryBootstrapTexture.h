#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/RenderPassTexturePool.h"

namespace RenderPassTexturePool
{
	//provide a typed zero texture before a history resource has a previous frame.
	struct HistoryBootstrapTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		TextureView texture;
	};
} //namespace RenderPassTexturePool
