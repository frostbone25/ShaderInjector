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

namespace ShaderResource
{
	struct ResolutionPolicyDisk
	{
		ResolutionMode mode = ResolutionMode::Inherit;
		uint32_t downscaleFactor = 1;
		uint32_t width = 0;
		uint32_t height = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ResolutionPolicyDisk,
			mode,
			downscaleFactor,
			width,
			height)
	};

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

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			TextureDescriptionDisk,
			dimension,
			format,
			resolution,
			depth,
			arraySize,
			mipLevels,
			sampleCount,
			lifetime,
			matchReferenceTexture,
			allowRenderTarget,
			allowUnorderedAccess)
	};

	const char* TextureDimensionName(TextureDimension dimension);

	const char* TextureHlslTypeName(TextureDimension dimension);

	struct TextureDisk
	{
		// Portable path relative to ShaderInjector/ShaderResources.
		std::string id;
		std::string name;
		std::string fileName;
		std::string filePath;

		// DDS metadata is inspected once when the resource database is refreshed.
		TextureDimension dimension = TextureDimension::Unknown;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t format = 0;
		std::string validationError;
	};

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

	struct TextureFormatOption
	{
		uint32_t value = 0;
		const char* name = "DXGI_FORMAT_UNKNOWN";
		const char* displayName = "DXGI_FORMAT_UNKNOWN (0)";
	};

	const char* ResourceOriginName(ResourceOrigin origin);
	const char* ResourceLifetimeName(ResourceLifetime lifetime);
	const std::vector<TextureFormatOption>& TextureFormatOptions();
	std::string TextureFormatDisplayName(uint32_t format);
	bool IsValidDownscaleFactor(uint32_t downscaleFactor);
}
