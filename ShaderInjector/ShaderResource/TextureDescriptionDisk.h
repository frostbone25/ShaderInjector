#pragma once

#include <cstdint>

#include "Enum/ShaderResourceResourceLifetime.h"
#include "Enum/ShaderResourceTextureDimension.h"
#include "JsonHelper.h"
#include "ShaderResource/ResolutionPolicyDisk.h"

namespace ShaderResource
{
	//persist the allocation policy for a runtime texture without storing GPU objects.
	struct TextureDescriptionDisk
	{
		TextureDimension dimension = TextureDimension::Texture2D;
		uint32_t format = 0;
		ResolutionPolicyDisk resolution;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t sampleCount = 1;
		ResourceLifetime lifetime = ResourceLifetime::Transient;
		bool matchReferenceTexture = false;
		bool allowRenderTarget = false;
		bool allowUnorderedAccess = true;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TextureDescriptionDisk, dimension, format, resolution, depth, arraySize, mipLevels, sampleCount, lifetime, matchReferenceTexture, allowRenderTarget, allowUnorderedAccess)
	};
} //namespace ShaderResource
