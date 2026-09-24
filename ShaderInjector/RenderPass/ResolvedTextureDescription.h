#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Enum/ShaderResourceResourceLifetime.h"
#include "Enum/ShaderResourceTextureDimension.h"

namespace RenderPassTexturePool
{
	//store the concrete allocation settings after resolving the pass's size policy.
	struct ResolvedTextureDescription
	{
		ShaderResource::TextureDimension dimension = ShaderResource::TextureDimension::Unknown;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT shaderViewFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t sampleCount = 1;
		ShaderResource::ResourceLifetime lifetime = ShaderResource::ResourceLifetime::Transient;
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
	};
} //namespace RenderPassTexturePool
