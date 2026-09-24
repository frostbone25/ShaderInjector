#pragma once

#include <cstdint>

#include "Enum/ShaderType.h"

namespace ShaderTarget
{
	//a bytecode hash needs its shader stage to identify a captured shader uniquely.
	struct ShaderIdentityKey
	{
		uint64_t shaderHash = 0;
		ShaderType shaderType = Unknown;

		bool operator==(const ShaderIdentityKey& other) const
		{
			return shaderHash == other.shaderHash && shaderType == other.shaderType;
		}
	};
} //namespace ShaderTarget
