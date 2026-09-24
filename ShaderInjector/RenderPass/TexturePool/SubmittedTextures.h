#pragma once

#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/TexturePool/RecordedTextureVersion.h"

namespace RenderPassTexturePool
{
	//retire a submitted texture batch after its queue fence reaches this value.
	struct SubmittedTextures
	{
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		UINT64 fenceValue = 0;
		std::vector<RecordedTextureVersion> versions;
	};
} //namespace RenderPassTexturePool
