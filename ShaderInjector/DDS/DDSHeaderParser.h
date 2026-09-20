#pragma once

#include <fstream>
#include <string>

#include "DDS/DDSMetadata.h"

namespace DDS::Internal
{
	bool OpenAndReadHeader(const std::string& filePath, std::ifstream& file,
		Metadata& outMetadata, std::streamoff& outPixelOffset, std::string& outError);
}
