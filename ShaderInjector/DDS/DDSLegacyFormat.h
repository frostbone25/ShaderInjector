#pragma once

#include <dxgiformat.h>

#include "DDS/DDSPixelFormat.h"

namespace DDS::Internal
{
	DXGI_FORMAT LegacyFormat(const DDSPixelFormat& format);
}
