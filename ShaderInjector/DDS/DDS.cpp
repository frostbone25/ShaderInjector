#include "DDS/DDS.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

#include "DDS/DDSHeader.h"
#include "DDS/DDSHeaderDX10.h"

namespace DDS
{
	//read and validate the DDS header without loading the image payload.
	//this is useful when the caller only needs the texture dimensions, format, mip count, or resource shape.
	// - filePath points to the DDS file on disk.
	// - outMetadata receives the parsed texture description when the method succeeds.
	// - outError receives a human-readable failure reason when the method returns false.
	bool ReadMetadata(const std::string& filePath, Metadata& outMetadata, std::string& outError)
	{
		outMetadata = {};

		std::ifstream ddsFile;
		std::streamoff pixelDataOffset = 0;

		if (!OpenAndReadDDSHeader(filePath, ddsFile, outMetadata, pixelDataOffset, outError))
			return false;

		//move to the end so we can make sure the header is followed by image data.
		ddsFile.seekg(0, std::ios::end);

		if (!ddsFile || ddsFile.tellg() <= pixelDataOffset)
		{
			outMetadata = {};
			outError = "DDS contains no image data. File: " + filePath;
			return false;
		}

		return true;
	}

	//open a DDS file, parse its header, and load the remaining bytes as the image payload.
	// - filePath points to the DDS file on disk.
	// - outImage receives both the parsed metadata and the raw pixel or block-compressed bytes.
	// - outError receives a human-readable failure reason when the method returns false.
	bool Load(const std::string& filePath, Image& outImage, std::string& outError)
	{
		outImage = {};

		std::ifstream ddsFile;
		std::streamoff pixelDataOffset = 0;

		if (!OpenAndReadDDSHeader(filePath, ddsFile, outImage.metadata, pixelDataOffset, outError))
			return false;

		//the stream is already positioned immediately after the header, so the remaining bytes are the image payload.
		outImage.pixels.assign(std::istreambuf_iterator<char>(ddsFile), {});

		if (outImage.pixels.empty())
		{
			outImage = {};
			outError = "DDS contains no image data. File: " + filePath;
			return false;
		}

		return true;
	}

	//translate a legacy DDS pixel format into the matching DXGI format.
	// - format contains the channel masks or four-character compression code from the legacy header.
	// - returns DXGI_FORMAT_UNKNOWN when the legacy format is not supported.
	DXGI_FORMAT LegacyFormat(const DDSPixelFormat& pixelFormat)
	{
		//compressed legacy DDS files identify their format with a four-character code.
		if (pixelFormat.flags & fourCCFlag)
		{
			switch (pixelFormat.fourCC)
			{
				case fourCC_DXT1:
					return DXGI_FORMAT_BC1_UNORM;
				case fourCC_DXT3:
					return DXGI_FORMAT_BC2_UNORM;
				case fourCC_DXT5:
					return DXGI_FORMAT_BC3_UNORM;
				case fourCC_ATI1:
					return DXGI_FORMAT_BC4_UNORM;
				case fourCC_ATI2:
					return DXGI_FORMAT_BC5_UNORM;
			}
		}

		//uncompressed legacy files use channel masks instead of a DX10 format value.
		if ((pixelFormat.flags & rgbFlag) && pixelFormat.rgbBitCount == 32)
		{
			if (pixelFormat.redMask == 0x000000ff && pixelFormat.greenMask == 0x0000ff00 && pixelFormat.blueMask == 0x00ff0000)
				return DXGI_FORMAT_R8G8B8A8_UNORM;

			if (pixelFormat.redMask == 0x00ff0000 && pixelFormat.greenMask == 0x0000ff00 && pixelFormat.blueMask == 0x000000ff)
				return DXGI_FORMAT_B8G8R8A8_UNORM;
		}

		return DXGI_FORMAT_UNKNOWN;
	}

	bool ReadDDSHeader(std::ifstream& ddsFile, Metadata& outMetadata, std::streamoff& outPixelDataOffset, std::string& outError)
	{
		uint32_t magicValue = 0;
		DDSHeader ddsHeader{};

		//read the fixed-size header before looking at the format-specific fields.
		ddsFile.read(reinterpret_cast<char*>(&magicValue), sizeof(magicValue));
		ddsFile.read(reinterpret_cast<char*>(&ddsHeader), sizeof(ddsHeader));

		if (!ddsFile || magicValue != ddsMagic || ddsHeader.size != sizeof(DDSHeader) || ddsHeader.pixelFormat.size != sizeof(DDSPixelFormat))
		{
			outError = "DDS header is invalid.";
			return false;
		}

		Metadata parsedMetadata{};
		parsedMetadata.width = ddsHeader.width;
		parsedMetadata.height = ddsHeader.height;
		parsedMetadata.mipLevels = (std::max)(1u, ddsHeader.mipMapCount);

		//a DX10 marker means the extended header contains the resource dimension and array information.
		if (ddsHeader.pixelFormat.fourCC == fourCC_DX10)
		{
			DDSHeaderDX10 directX10Header{};
			ddsFile.read(reinterpret_cast<char*>(&directX10Header), sizeof(directX10Header));

			if (!ddsFile || !directX10Header.arraySize)
			{
				outError = "DDS DX10 header is invalid.";
				return false;
			}

			parsedMetadata.format = directX10Header.format;

			switch (directX10Header.resourceDimension)
			{
				case 3: //D3D10_RESOURCE_DIMENSION_TEXTURE2D
				{
					parsedMetadata.depth = 1;

					if (directX10Header.miscFlag & ddsResourceMiscTextureCube)
					{
						if (directX10Header.arraySize > (std::numeric_limits<uint32_t>::max)() / 6u)
						{
							outError = "DDS cube-array size is invalid.";
							return false;
						}

						parsedMetadata.arraySize = directX10Header.arraySize * 6u;

						if (directX10Header.arraySize > 1)
							parsedMetadata.dimension = ShaderResource::TextureDimension::TextureCubeArray;
						else
							parsedMetadata.dimension = ShaderResource::TextureDimension::TextureCube;
					}
					else
					{
						parsedMetadata.arraySize = directX10Header.arraySize;

						if (directX10Header.arraySize > 1)
							parsedMetadata.dimension = ShaderResource::TextureDimension::Texture2DArray;
						else
							parsedMetadata.dimension = ShaderResource::TextureDimension::Texture2D;
					}

					break;
				}
				case 4: //D3D10_RESOURCE_DIMENSION_TEXTURE3D
				{
					if (directX10Header.arraySize != 1 || (directX10Header.miscFlag & ddsResourceMiscTextureCube) || !ddsHeader.depth)
					{
						outError = "DDS Texture3D metadata is invalid.";
						return false;
					}

					parsedMetadata.dimension = ShaderResource::TextureDimension::Texture3D;
					parsedMetadata.depth = ddsHeader.depth;
					parsedMetadata.arraySize = 1;
					break;
				}
				default:
					outError = "Only Texture2D, Texture2DArray, TextureCube, and Texture3D DDS resources are supported.";
					return false;
			}
		}
		else
		{
			//older DDS files describe the resource shape through the legacy caps fields.
			parsedMetadata.format = LegacyFormat(ddsHeader.pixelFormat);

			if (ddsHeader.caps2 & ddsCaps2Volume)
			{
				if (!ddsHeader.depth)
				{
					outError = "Legacy DDS Texture3D depth is invalid.";
					return false;
				}

				parsedMetadata.dimension = ShaderResource::TextureDimension::Texture3D;
				parsedMetadata.depth = ddsHeader.depth;
				parsedMetadata.arraySize = 1;
			}
			else if (ddsHeader.caps2 & ddsCaps2CubeMap)
			{
				if ((ddsHeader.caps2 & ddsCaps2CubeMapAllFaces) != ddsCaps2CubeMapAllFaces)
				{
					outError = "Partial legacy DDS cubemaps are unsupported.";
					return false;
				}

				parsedMetadata.dimension = ShaderResource::TextureDimension::TextureCube;
				parsedMetadata.depth = 1;
				parsedMetadata.arraySize = 6;
			}
			else
			{
				parsedMetadata.dimension = ShaderResource::TextureDimension::Texture2D;
				parsedMetadata.depth = 1;
				parsedMetadata.arraySize = 1;
			}
		}

		//reject unsupported formats and dimensions before exposing partially parsed metadata.
		if (!parsedMetadata.width || !parsedMetadata.height || parsedMetadata.format == DXGI_FORMAT_UNKNOWN)
		{
			outError = "DDS format or dimensions are unsupported.";
			return false;
		}

		if (parsedMetadata.arraySize > (std::numeric_limits<uint16_t>::max)() || parsedMetadata.depth > (std::numeric_limits<uint16_t>::max)() || parsedMetadata.mipLevels > (std::numeric_limits<uint16_t>::max)())
		{
			outError = "DDS array, depth, or mip count exceeds D3D12 limits.";
			return false;
		}

		outPixelDataOffset = ddsFile.tellg();

		if (outPixelDataOffset <= 0)
		{
			outError = "DDS pixel-data offset is invalid.";
			return false;
		}

		outMetadata = parsedMetadata;
		return true;
	}

	//open a DDS file and leave ddsFile positioned at the first byte after its header.
	//this helper is used by the loader so metadata parsing and payload reading share the same stream.
	// - filePath points to the DDS file on disk.
	// - ddsFile receives the open binary stream and remains owned by the caller.
	// - outMetadata receives the parsed texture description.
	// - outPixelDataOffset receives the byte offset where the image payload begins.
	// - outError receives a human-readable failure reason when the method returns false.
	bool OpenAndReadDDSHeader(const std::string& filePath, std::ifstream& ddsFile, Metadata& outMetadata, std::streamoff& outPixelDataOffset, std::string& outError)
	{
		outError.clear();
		ddsFile.open(std::filesystem::u8path(filePath), std::ios::binary);

		if (!ddsFile)
		{
			outError = "Could not open DDS file: " + filePath;
			return false;
		}

		if (!ReadDDSHeader(ddsFile, outMetadata, outPixelDataOffset, outError))
		{
			outError += " File: " + filePath;
			return false;
		}

		return true;
	}
} //namespace DDS
