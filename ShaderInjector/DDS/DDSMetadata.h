#pragma once

#include <cstdint>
#include <dxgiformat.h>

#include "Enum/ShaderResourceTextureDimension.h"

namespace DDS
{
	struct Metadata
	{
		ShaderResource::TextureDimension dimension = ShaderResource::TextureDimension::Unknown;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
	};
} //namespace DDS
