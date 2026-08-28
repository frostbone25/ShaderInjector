#pragma once

#include <cstdint>
#include <string>

#include "JsonHelper.h"

namespace ShaderResource
{
	enum class TextureDimension : uint8_t
	{
		Unknown,
		Texture2D,
		Texture2DArray,
		TextureCube,
		TextureCubeArray,
		Texture3D,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(TextureDimension,
	{
		{ TextureDimension::Unknown, "Unknown" },
		{ TextureDimension::Texture2D, "Texture2D" },
		{ TextureDimension::Texture2DArray, "Texture2DArray" },
		{ TextureDimension::TextureCube, "TextureCube" },
		{ TextureDimension::TextureCubeArray, "TextureCubeArray" },
		{ TextureDimension::Texture3D, "Texture3D" },
	})

	enum class ResourceOrigin : uint8_t
	{
		Disk,
		Game,
		Runtime,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResourceOrigin,
	{
		{ ResourceOrigin::Disk, "Disk" },
		{ ResourceOrigin::Game, "Game" },
		{ ResourceOrigin::Runtime, "Runtime" },
	})

	enum class ResourceLifetime : uint8_t
	{
		Immutable,
		Transient,
		Persistent,
		History,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResourceLifetime,
	{
		{ ResourceLifetime::Immutable, "Immutable" },
		{ ResourceLifetime::Transient, "Transient" },
		{ ResourceLifetime::Persistent, "Persistent" },
		{ ResourceLifetime::History, "History" },
	})

	enum class ResolutionMode : uint8_t
	{
		Inherit,
		DownscalePowerOfTwo,
		Explicit,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResolutionMode,
	{
		{ ResolutionMode::Inherit, "Inherit" },
		{ ResolutionMode::DownscalePowerOfTwo, "DownscalePowerOfTwo" },
		{ ResolutionMode::Explicit, "Explicit" },
	})

	enum class TemporalView : uint8_t
	{
		Current,
		Previous,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(TemporalView,
	{
		{ TemporalView::Current, "Current" },
		{ TemporalView::Previous, "Previous" },
	})

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

	inline const char* TextureDimensionName(TextureDimension dimension)
	{
		switch (dimension)
		{
			case TextureDimension::Texture2D: return "Texture2D";
			case TextureDimension::Texture2DArray: return "Texture2DArray";
			case TextureDimension::TextureCube: return "TextureCube";
			case TextureDimension::TextureCubeArray: return "TextureCubeArray";
			case TextureDimension::Texture3D: return "Texture3D";
			case TextureDimension::Unknown:
			default: return "Unknown";
		}
	}

	inline const char* TextureHlslTypeName(TextureDimension dimension)
	{
		switch (dimension)
		{
			case TextureDimension::Texture2DArray: return "Texture2DArray<float4>";
			case TextureDimension::TextureCube: return "TextureCube<float4>";
			case TextureDimension::TextureCubeArray: return "TextureCubeArray<float4>";
			case TextureDimension::Texture3D: return "Texture3D<float4>";
			case TextureDimension::Texture2D:
			case TextureDimension::Unknown:
			default: return "Texture2D<float4>";
		}
	}

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

	const char* ResourceOriginName(ResourceOrigin origin);
	const char* ResourceLifetimeName(ResourceLifetime lifetime);
	bool IsValidDownscaleFactor(uint32_t downscaleFactor);
}
