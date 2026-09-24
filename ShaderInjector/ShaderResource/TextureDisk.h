#pragma once

#include <cstdint>
#include <string>

#include "Enum/ShaderResourceTextureDimension.h"

namespace ShaderResource
{
	//keep one texture's portable path and cached DDS metadata from database refresh.
	struct TextureDisk
	{
		std::string id;
		std::string name;
		std::string fileName;
		std::string filePath;
		TextureDimension dimension = TextureDimension::Unknown;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t format = 0;
		std::string validationError;
	};
} //namespace ShaderResource
