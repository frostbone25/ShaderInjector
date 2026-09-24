#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Enum/ShaderResourceTextureDimension.h"

namespace RenderPassTexturePool
{
	//describe the game image that a relative-size runtime texture follows.
	struct ReferenceExtent
	{
		ShaderResource::TextureDimension dimension = ShaderResource::TextureDimension::Unknown;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t sampleCount = 1;
		DXGI_FORMAT fallbackFormat = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT fallbackShaderViewFormat = DXGI_FORMAT_UNKNOWN;
	};
} //namespace RenderPassTexturePool
