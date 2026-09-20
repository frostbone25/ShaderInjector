#include "DDS/DDSHeaderParser.h"

#include <algorithm>
#include <filesystem>
#include <limits>

#include "DDS/DDSConstants.h"
#include "DDS/DDSHeader.h"
#include "DDS/DDSHeaderDx10.h"
#include "DDS/DDSLegacyFormat.h"

namespace DDS::Internal
{
	bool ReadHeader(
		std::ifstream& file,
		Metadata& outMetadata,
		std::streamoff& outPixelOffset,
		std::string& outError)
	{
		uint32_t magic = 0;
		DdsHeader header{};
		file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!file || magic != ddsMagic || header.size != sizeof(DdsHeader) ||
			header.pixelFormat.size != sizeof(DdsPixelFormat))
		{
			outError = "DDS header is invalid.";
			return false;
		}

		Metadata metadata{};
		metadata.width = header.width;
		metadata.height = header.height;
		metadata.mipLevels = (std::max)(1u, header.mipMapCount);

		if (header.pixelFormat.fourCC == fourCcDx10)
		{
			DdsHeaderDx10 dx10{};
			file.read(reinterpret_cast<char*>(&dx10), sizeof(dx10));
			if (!file || !dx10.arraySize)
			{
				outError = "DDS DX10 header is invalid.";
				return false;
			}

			metadata.format = dx10.format;
			switch (dx10.resourceDimension)
			{
				case 3: // D3D10_RESOURCE_DIMENSION_TEXTURE2D
				{
					metadata.depth = 1;
					if (dx10.miscFlag & ddsResourceMiscTextureCube)
					{
						if (dx10.arraySize > (std::numeric_limits<uint32_t>::max)() / 6u)
						{
							outError = "DDS cube-array size is invalid.";
							return false;
						}
						metadata.arraySize = dx10.arraySize * 6u;
						metadata.dimension = dx10.arraySize > 1
							? ShaderResource::TextureDimension::TextureCubeArray
							: ShaderResource::TextureDimension::TextureCube;
					}
					else
					{
						metadata.arraySize = dx10.arraySize;
						metadata.dimension = dx10.arraySize > 1
							? ShaderResource::TextureDimension::Texture2DArray
							: ShaderResource::TextureDimension::Texture2D;
					}
					break;
				}
				case 4: // D3D10_RESOURCE_DIMENSION_TEXTURE3D
					if (dx10.arraySize != 1 || (dx10.miscFlag & ddsResourceMiscTextureCube) || !header.depth)
					{
						outError = "DDS Texture3D metadata is invalid.";
						return false;
					}
					metadata.dimension = ShaderResource::TextureDimension::Texture3D;
					metadata.depth = header.depth;
					metadata.arraySize = 1;
					break;
				default:
					outError = "Only Texture2D, Texture2DArray, TextureCube, and Texture3D DDS resources are supported.";
					return false;
			}
		}
		else
		{
			metadata.format = LegacyFormat(header.pixelFormat);
			if (header.caps2 & ddsCaps2Volume)
			{
				if (!header.depth)
				{
					outError = "Legacy DDS Texture3D depth is invalid.";
					return false;
				}
				metadata.dimension = ShaderResource::TextureDimension::Texture3D;
				metadata.depth = header.depth;
				metadata.arraySize = 1;
			}
			else if (header.caps2 & ddsCaps2CubeMap)
			{
				if ((header.caps2 & ddsCaps2CubeMapAllFaces) != ddsCaps2CubeMapAllFaces)
				{
					outError = "Partial legacy DDS cubemaps are unsupported.";
					return false;
				}
				metadata.dimension = ShaderResource::TextureDimension::TextureCube;
				metadata.depth = 1;
				metadata.arraySize = 6;
			}
			else
			{
				metadata.dimension = ShaderResource::TextureDimension::Texture2D;
				metadata.depth = 1;
				metadata.arraySize = 1;
			}
		}

		if (!metadata.width || !metadata.height || metadata.format == DXGI_FORMAT_UNKNOWN)
		{
			outError = "DDS format or dimensions are unsupported.";
			return false;
		}
		if (metadata.arraySize > (std::numeric_limits<uint16_t>::max)() ||
			metadata.depth > (std::numeric_limits<uint16_t>::max)() ||
			metadata.mipLevels > (std::numeric_limits<uint16_t>::max)())
		{
			outError = "DDS array, depth, or mip count exceeds D3D12 limits.";
			return false;
		}

		outPixelOffset = file.tellg();
		if (outPixelOffset <= 0)
		{
			outError = "DDS pixel-data offset is invalid.";
			return false;
		}
		outMetadata = metadata;
		return true;
	}

	bool OpenAndReadHeader(
		const std::string& filePath,
		std::ifstream& file,
		Metadata& outMetadata,
		std::streamoff& outPixelOffset,
		std::string& outError)
	{
		outError.clear();
		file.open(std::filesystem::u8path(filePath), std::ios::binary);
		if (!file)
		{
			outError = "Could not open DDS file: " + filePath;
			return false;
		}
		if (!ReadHeader(file, outMetadata, outPixelOffset, outError))
		{
			outError += " File: " + filePath;
			return false;
		}
		return true;
	}
}
