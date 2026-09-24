#pragma once
#include "Enum/ShaderResourceTextureDimension.h"
#include "Enum/ShaderResourceResourceOrigin.h"
#include "Enum/ShaderResourceResourceLifetime.h"
#include "Enum/ShaderResourceResolutionMode.h"
#include "Enum/ShaderResourceTemporalView.h"

#include <cstdint>
#include <string>
#include <vector>

#include "JsonHelper.h"
#include "ShaderResource/CatalogEntry.h"
#include "ShaderResource/ResolutionPolicyDisk.h"
#include "ShaderResource/TextureDescriptionDisk.h"
#include "ShaderResource/TextureDisk.h"
#include "ShaderResource/TextureFormatOption.h"

namespace ShaderResource
{
	const char* TextureDimensionName(TextureDimension dimension);

	const char* TextureHlslTypeName(TextureDimension dimension);

	const char* ResourceOriginName(ResourceOrigin origin);
	const char* ResourceLifetimeName(ResourceLifetime lifetime);
	const std::vector<TextureFormatOption>& TextureFormatOptions();
	std::string TextureFormatDisplayName(uint32_t format);
	bool IsValidDownscaleFactor(uint32_t downscaleFactor);
} //namespace ShaderResource
