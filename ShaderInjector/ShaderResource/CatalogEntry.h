#pragma once

#include <cstdint>
#include <string>

#include "Enum/ShaderResourceResourceLifetime.h"
#include "Enum/ShaderResourceResourceOrigin.h"
#include "Enum/ShaderResourceTextureDimension.h"

namespace ShaderResource
{
	//show a resource's origin, owner, dimensions, and residency in the catalog.
	struct CatalogEntry
	{
		std::string id;
		std::string name;
		ResourceOrigin origin = ResourceOrigin::Disk;
		ResourceLifetime lifetime = ResourceLifetime::Immutable;
		std::string ownerRenderPassId;
		TextureDimension dimension = TextureDimension::Unknown;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t format = 0;
		bool resident = false;
		std::string status;
	};
} //namespace ShaderResource
