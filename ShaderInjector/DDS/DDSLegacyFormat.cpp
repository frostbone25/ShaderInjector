#include "DDS/DDSLegacyFormat.h"

#include "DDS/DDSConstants.h"

namespace DDS::Internal
{
	DXGI_FORMAT LegacyFormat(const DdsPixelFormat& format)
	{
		if (format.flags & fourCcFlag)
		{
			switch (format.fourCC)
			{
				case fourCcDxt1: return DXGI_FORMAT_BC1_UNORM;
				case fourCcDxt3: return DXGI_FORMAT_BC2_UNORM;
				case fourCcDxt5: return DXGI_FORMAT_BC3_UNORM;
				case fourCcAti1: return DXGI_FORMAT_BC4_UNORM;
				case fourCcAti2: return DXGI_FORMAT_BC5_UNORM;
			}
		}

		if ((format.flags & rgbFlag) && format.rgbBitCount == 32)
		{
			if (format.redMask == 0x000000ff && format.greenMask == 0x0000ff00 &&
				format.blueMask == 0x00ff0000)
			{
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			}
			if (format.redMask == 0x00ff0000 && format.greenMask == 0x0000ff00 &&
				format.blueMask == 0x000000ff)
			{
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			}
		}

		return DXGI_FORMAT_UNKNOWN;
	}
}
