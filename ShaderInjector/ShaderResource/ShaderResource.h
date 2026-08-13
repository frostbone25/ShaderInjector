#pragma once

#include <cstdint>
#include <string>

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
}
