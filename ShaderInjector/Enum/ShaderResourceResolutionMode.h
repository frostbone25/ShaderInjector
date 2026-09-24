#pragma once

#include <cstdint>
#include "JsonHelper.h"

namespace ShaderResource
{
	enum class ResolutionMode : uint8_t
	{
		Inherit,
		DownscalePowerOfTwo,
		Explicit,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResolutionMode,
								 {
									 {ResolutionMode::Inherit, "Inherit"},
									 {ResolutionMode::DownscalePowerOfTwo, "DownscalePowerOfTwo"},
									 {ResolutionMode::Explicit, "Explicit"},
								 })
} //namespace ShaderResource
