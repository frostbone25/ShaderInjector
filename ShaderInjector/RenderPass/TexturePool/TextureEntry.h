#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/RenderPassTexturePool.h"
#include "RenderPass/TexturePool/DefinitionRecord.h"
#include "RenderPass/TexturePool/TextureVersion.h"

namespace RenderPassTexturePool
{
	//track an allocation and both temporal versions across configuration changes.
	struct TextureEntry
	{
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		DefinitionRecord definition;
		ResolvedTextureDescription description;
		std::array<TextureVersion, 2> versions;
		uint32_t versionCount = 0;
		uint64_t generation = 0;
		uint64_t allocationBytes = 0;
	};
} //namespace RenderPassTexturePool
