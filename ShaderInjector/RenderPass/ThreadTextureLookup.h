#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "RenderPass/RenderPassTexturePool.h"

namespace RenderPassTexturePool
{
	//reuse a texture view only while the frame and command-list recording still match.
	struct ThreadTextureLookup
	{
		uint64_t textureEpoch = 0;
		uint64_t frameIndex = 0;
		std::shared_ptr<std::atomic<uint64_t>> recordingEpoch;
		uint64_t recordingValue = 0;
		ID3D12GraphicsCommandList* recordedCommandList = nullptr;
		bool frameSensitive = false;
		TextureView texture;
	};
} //namespace RenderPassTexturePool
