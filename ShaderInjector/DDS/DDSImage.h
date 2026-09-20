#pragma once

#include <cstdint>
#include <vector>

#include "DDS/DDSMetadata.h"

namespace DDS
{
	struct Image
	{
		Metadata metadata;
		std::vector<uint8_t> pixels;
	};
}
