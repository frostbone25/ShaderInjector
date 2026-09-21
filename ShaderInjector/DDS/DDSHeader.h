#pragma once

#include <cstdint>

#include "DDS/DDSPixelFormat.h"

namespace DDS
{
#pragma pack(push, 1)
	struct DDSHeader
	{
		uint32_t size;
		uint32_t flags;
		uint32_t height;
		uint32_t width;
		uint32_t pitchOrLinearSize;
		uint32_t depth;
		uint32_t mipMapCount;
		uint32_t reserved1[11];
		DDSPixelFormat pixelFormat;
		uint32_t caps;
		uint32_t caps2;
		uint32_t caps3;
		uint32_t caps4;
		uint32_t reserved2;
	};
#pragma pack(pop)

	static_assert(sizeof(DDSHeader) == 124, "Unexpected DDS header size.");
}
