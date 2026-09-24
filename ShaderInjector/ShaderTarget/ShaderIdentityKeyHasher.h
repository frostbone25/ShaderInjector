#pragma once

#include <cstddef>
#include <cstdint>

#include "ShaderIdentityKey.h"

namespace ShaderTarget
{
	//mix the shader stage into the hash before using the identity in a cache.
	struct ShaderIdentityKeyHasher
	{
		size_t operator()(const ShaderIdentityKey& shaderIdentity) const
		{
			return static_cast<size_t>(shaderIdentity.shaderHash ^ (static_cast<uint64_t>(shaderIdentity.shaderType) << 57));
		}
	};
} //namespace ShaderTarget
