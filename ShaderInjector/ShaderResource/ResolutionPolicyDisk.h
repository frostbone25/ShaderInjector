#pragma once

#include <cstdint>

#include "Enum/ShaderResourceResolutionMode.h"
#include "JsonHelper.h"

namespace ShaderResource
{
	//save how a resource chooses its size from the game image or explicit dimensions.
	struct ResolutionPolicyDisk
	{
		ResolutionMode mode = ResolutionMode::Inherit;
		uint32_t downscaleFactor = 1;
		uint32_t width = 0;
		uint32_t height = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ResolutionPolicyDisk, mode, downscaleFactor, width, height)
	};
} //namespace ShaderResource
