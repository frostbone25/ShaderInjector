#pragma once

#include <cstdint>

#include "DDS/DDSPixelFormat.h"

namespace DDS
{
	//reference - https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header

#pragma pack(push, 1)
	struct DDSHeader
	{
		//size of structure.
		//this member must be set to 124.
		uint32_t size;

		//flags to indicate which members contain valid data.
		uint32_t flags;

		//surface height (in pixels).
		uint32_t height;

		//surface width (in pixels).
		uint32_t width;

		//the pitch or number of bytes per scan line in an uncompressed texture
		//the total number of bytes in the top level texture for a compressed texture. 
		//for information about how to compute the pitch, see the DDS File Layout section of the Programming Guide for DDS.
		uint32_t pitchOrLinearSize;

		//depth of a volume texture (in pixels), otherwise unused.
		uint32_t depth;

		//number of mipmap levels, otherwise unused.
		uint32_t mipMapCount;

		//unused.
		uint32_t reserved1[11];
		DDSPixelFormat pixelFormat;

		//specifies the complexity of the surfaces stored.
		uint32_t caps;

		//additional detail about the surfaces stored.
		uint32_t caps2;

		//unused.
		uint32_t caps3;

		//unused.
		uint32_t caps4;

		//unused.
		uint32_t reserved2;
	};
#pragma pack(pop)

	static_assert(sizeof(DDSHeader) == 124, "Unexpected DDS header size.");
}
