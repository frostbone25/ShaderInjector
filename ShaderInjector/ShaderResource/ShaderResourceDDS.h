#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <dxgiformat.h>

#include "ShaderResource/ShaderResource.h"

namespace ShaderResourceDDS
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

	struct Image
	{
		Metadata metadata;
		std::vector<uint8_t> pixels;
	};

	bool ReadMetadata(const std::string& filePath, Metadata& outMetadata, std::string& outError);
	bool Load(const std::string& filePath, Image& outImage, std::string& outError);
}
