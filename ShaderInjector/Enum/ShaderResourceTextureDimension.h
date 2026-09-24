#pragma once

#include <cstdint>
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
									 {TextureDimension::Unknown, "Unknown"},
									 {TextureDimension::Texture2D, "Texture2D"},
									 {TextureDimension::Texture2DArray, "Texture2DArray"},
									 {TextureDimension::TextureCube, "TextureCube"},
									 {TextureDimension::TextureCubeArray, "TextureCubeArray"},
									 {TextureDimension::Texture3D, "Texture3D"},
								 })
} //namespace ShaderResource
