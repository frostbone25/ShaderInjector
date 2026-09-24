#pragma once

#include <cstdint>
#include "JsonHelper.h"

namespace ShaderResource
{
	enum class ResourceLifetime : uint8_t
	{
		Immutable,
		Transient,
		Persistent,
		History,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResourceLifetime,
								 {
									 {ResourceLifetime::Immutable, "Immutable"},
									 {ResourceLifetime::Transient, "Transient"},
									 {ResourceLifetime::Persistent, "Persistent"},
									 {ResourceLifetime::History, "History"},
								 })
} //namespace ShaderResource
