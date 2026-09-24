#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "RenderPass/RenderPassTexturePool.h"

namespace RenderPassTexturePool
{
	//remember one pass's resource state while its command-list recording epoch is current.
	struct ThreadTextureCacheEntry
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		ID3D12GraphicsCommandList* commandList = nullptr;
		uint64_t configurationGeneration = 0;
		uint64_t textureEpoch = 0;
		std::shared_ptr<std::atomic<uint64_t>> recordingEpoch;
		uint64_t recordingValue = 0;
		ReferenceExtent referenceExtent;
		bool succeeded = false;
		std::string error;
	};
} //namespace RenderPassTexturePool
