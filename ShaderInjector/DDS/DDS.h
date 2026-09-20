#pragma once

#include <string>

#include "DDS/DDSImage.h"
#include "DDS/DDSMetadata.h"

namespace DDS
{
	bool ReadMetadata(const std::string& filePath, Metadata& outMetadata, std::string& outError);
	bool Load(const std::string& filePath, Image& outImage, std::string& outError);
}
