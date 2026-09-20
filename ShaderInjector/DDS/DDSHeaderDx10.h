#pragma once

#include <cstdint>
#include <dxgiformat.h>

namespace DDS
{
#pragma pack(push, 1)
	struct DdsHeaderDx10
	{
		DXGI_FORMAT format;
		uint32_t resourceDimension;
		uint32_t miscFlag;
		uint32_t arraySize;
		uint32_t miscFlags2;
	};
#pragma pack(pop)

	static_assert(sizeof(DdsHeaderDx10) == 20, "Unexpected DDS DX10 header size.");
}
