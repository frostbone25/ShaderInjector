#include "DDS/DDS.h"

#include <fstream>
#include <iterator>

#include "DDS/DDSHeaderParser.h"

namespace DDS
{
	bool ReadMetadata(const std::string& filePath, Metadata& outMetadata, std::string& outError)
	{
		outMetadata = {};

		std::ifstream file;
		std::streamoff pixelOffset = 0;

		if (!Internal::OpenAndReadHeader(filePath, file, outMetadata, pixelOffset, outError))
			return false;

		file.seekg(0, std::ios::end);

		if (!file || file.tellg() <= pixelOffset)
		{
			outMetadata = {};
			outError = "DDS contains no image data. File: " + filePath;
			return false;
		}

		return true;
	}

	bool Load(const std::string& filePath, Image& outImage, std::string& outError)
	{
		outImage = {};

		std::ifstream file;
		std::streamoff pixelOffset = 0;

		if (!Internal::OpenAndReadHeader(filePath, file, outImage.metadata, pixelOffset, outError))
			return false;

		outImage.pixels.assign(std::istreambuf_iterator<char>(file), {});

		if (outImage.pixels.empty())
		{
			outImage = {};
			outError = "DDS contains no image data. File: " + filePath;
			return false;
		}

		return true;
	}
}
