#pragma once

#include <cstdint>

namespace DDS
{
	//reference - https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-pixelformat

#pragma pack(push, 1)
	struct DDSPixelFormat
	{
		//structure size
		//set to 32 (bytes).
		uint32_t size;

		//values which indicate what type of data is in the surface.
		uint32_t flags;

		//four - character codes for specifying compressed or custom formats.
		//possible values include : DXT1, DXT2, DXT3, DXT4, or DXT5.
		//a FourCC of DX10 indicates the prescense of the DDS_HEADER_DXT10 extended header, and the dxgiFormat member of that structure indicates the true format.
		//when using a four - character code, dwFlags must include DDPF_FOURCC.
		uint32_t fourCC;

		//number of bits in an RGB (possibly including alpha) format. 
		//valid when dwFlags includes DDPF_RGB, DDPF_LUMINANCE, or DDPF_YUV.
		uint32_t rgbBitCount;

		//red (or luminance or Y) mask for reading color data. 
		//for instance, given the A8R8G8B8 format, the red mask would be 0x00ff0000.
		uint32_t redMask;

		//green (or U) mask for reading color data.
		//for instance, given the A8R8G8B8 format, the green mask would be 0x0000ff00.
		uint32_t greenMask;

		//blue (or V) mask for reading color data.
		//for instance, given the A8R8G8B8 format, the blue mask would be 0x000000ff.
		uint32_t blueMask;

		//alpha mask for reading alpha data.
		//dwFlags must include DDPF_ALPHAPIXELS or DDPF_ALPHA. 
		//for instance, given the A8R8G8B8 format, the alpha mask would be 0xff000000.
		uint32_t alphaMask;
	};
#pragma pack(pop)

	static_assert(sizeof(DDSPixelFormat) == 32, "Unexpected DDS pixel format size.");
}
