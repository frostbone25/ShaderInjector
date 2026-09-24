#pragma once

#include <cstdint>

namespace ShaderResource
{
	//pair a DXGI format value with its short and descriptive UI labels.
	struct TextureFormatOption
	{
		uint32_t value = 0;
		const char* name = "DXGI_FORMAT_UNKNOWN";
		const char* displayName = "DXGI_FORMAT_UNKNOWN (0)";
	};
} //namespace ShaderResource
