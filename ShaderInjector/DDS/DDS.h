#pragma once

#include <fstream>
#include <string>
#include <cstdint>
#include <dxgiformat.h>

#include "DDS/DDSImage.h"
#include "DDS/DDSMetadata.h"
#include "DDS/DDSPixelFormat.h"

namespace DDS
{
	//reference - https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide
	//constants

	//A DWORD (magic number) containing the four character code value 'DDS ' (0x20534444).
	constexpr uint32_t ddsMagic = 0x20534444;
	constexpr uint32_t fourCCFlag = 0x4;
	constexpr uint32_t rgbFlag = 0x40;
	constexpr uint32_t ddsCaps2CubeMap = 0x00000200;
	constexpr uint32_t ddsCaps2CubeMapAllFaces = 0x0000FC00;
	constexpr uint32_t ddsCaps2Volume = 0x00200000;
	constexpr uint32_t ddsResourceMiscTextureCube = 0x4;

	constexpr uint32_t FourCC(char a, char b, char c, char d)
	{
		return static_cast<uint8_t>(a) |
			   (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
			   (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
			   (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
	}

	constexpr uint32_t fourCC_DX10 = FourCC('D', 'X', '1', '0');
	constexpr uint32_t fourCC_DXT1 = FourCC('D', 'X', 'T', '1');
	constexpr uint32_t fourCC_DXT3 = FourCC('D', 'X', 'T', '3');
	constexpr uint32_t fourCC_DXT5 = FourCC('D', 'X', 'T', '5');
	constexpr uint32_t fourCC_ATI1 = FourCC('A', 'T', 'I', '1');
	constexpr uint32_t fourCC_ATI2 = FourCC('A', 'T', 'I', '2');

	//read and validate the DDS header without loading the image payload.
	//this is useful when the caller only needs the texture dimensions, format, mip count, or resource shape.
	// - filePath points to the DDS file on disk.
	// - outMetadata receives the parsed texture description when the method succeeds.
	// - outError receives a human-readable failure reason when the method returns false.
	bool ReadMetadata(const std::string& filePath, Metadata& outMetadata, std::string& outError);

	//open a DDS file, parse its header, and load the remaining bytes as the image payload.
	// - filePath points to the DDS file on disk.
	// - outImage receives both the parsed metadata and the raw pixel or block-compressed bytes.
	// - outError receives a human-readable failure reason when the method returns false.
	bool Load(const std::string& filePath, Image& outImage, std::string& outError);

	//open a DDS file and leave ddsFile positioned at the first byte after its header.
	//this helper is used by the loader so metadata parsing and payload reading share the same stream.
	// - filePath points to the DDS file on disk.
	// - ddsFile receives the open binary stream and remains owned by the caller.
	// - outMetadata receives the parsed texture description.
	// - outPixelDataOffset receives the byte offset where the image payload begins.
	// - outError receives a human-readable failure reason when the method returns false.
	bool OpenAndReadDDSHeader(const std::string& filePath, std::ifstream& ddsFile, Metadata& outMetadata, std::streamoff& outPixelDataOffset, std::string& outError);

	//translate a legacy DDS pixel format into the matching DXGI format.
	// - format contains the channel masks or four-character compression code from the legacy header.
	// - returns DXGI_FORMAT_UNKNOWN when the legacy format is not supported.
	DXGI_FORMAT LegacyFormat(const DDSPixelFormat& format);
} //namespace DDS
