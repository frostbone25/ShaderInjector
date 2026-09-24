#pragma once

#include <cstdint>
#include "JsonHelper.h"

namespace ShaderResource
{
	enum class ResourceOrigin : uint8_t
	{
		Disk,
		Game,
		Runtime,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResourceOrigin,
								 {
									 {ResourceOrigin::Disk, "Disk"},
									 {ResourceOrigin::Game, "Game"},
									 {ResourceOrigin::Runtime, "Runtime"},
								 })
} //namespace ShaderResource
