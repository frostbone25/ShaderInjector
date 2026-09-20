#pragma once

#include <cstdint>

namespace DDS
{
#pragma pack(push, 1)
	struct DdsPixelFormat
	{
		uint32_t size;
		uint32_t flags;
		uint32_t fourCC;
		uint32_t rgbBitCount;
		uint32_t redMask;
		uint32_t greenMask;
		uint32_t blueMask;
		uint32_t alphaMask;
	};
#pragma pack(pop)

	static_assert(sizeof(DdsPixelFormat) == 32, "Unexpected DDS pixel format size.");
}
