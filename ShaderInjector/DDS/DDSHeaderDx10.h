#pragma once

#include <cstdint>
#include <dxgiformat.h>

namespace DDS
{
	//reference - https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header-dxt10

#pragma pack(push, 1)
	struct DDSHeaderDX10
	{
		//the surface pixel format (see DXGI_FORMAT).
		DXGI_FORMAT format;

		//identifies the type of resource.
		//the following values for this member are a subset of the values in the D3D10_RESOURCE_DIMENSION or D3D11_RESOURCE_DIMENSION enumeration:
		uint32_t resourceDimension;

		//identifies other, less common options for resources. 
		//the following value for this member is a subset of the values in the D3D10_RESOURCE_MISC_FLAG or D3D11_RESOURCE_MISC_FLAG enumeration:
		uint32_t miscFlag;

		//the number of elements in the array.
		//for a 2D texture that is also a cube - map texture, this number represents the number of cubes.
		//this number is the same as the number in the NumCubes member of D3D10_TEXCUBE_ARRAY_SRV1 or D3D11_TEXCUBE_ARRAY_SRV).
		//in this case, the DDS file contains arraySize * 6 2D textures.
		//for more information about this case, see the miscFlag description.
		//for a 3D texture, you must set this number to 1.
		uint32_t arraySize;

		//contains additional metadata (formerly was reserved).
		//the lower 3 bits indicate the alpha mode of the associated resource.
		//the upper 29 bits are reserved and are typically 0.
		uint32_t miscFlags2;
	};
#pragma pack(pop)

	static_assert(sizeof(DDSHeaderDX10) == 20, "Unexpected DDS DX10 header size.");
}
