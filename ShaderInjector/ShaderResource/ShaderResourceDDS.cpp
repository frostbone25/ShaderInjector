#include "ShaderResource/ShaderResourceDDS.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

namespace ShaderResourceDDS
{
	namespace
	{
		constexpr uint32_t ddsMagic = 0x20534444;
		constexpr uint32_t fourCcFlag = 0x4;
		constexpr uint32_t rgbFlag = 0x40;
		constexpr uint32_t ddsCaps2CubeMap = 0x00000200;
		constexpr uint32_t ddsCaps2CubeMapAllFaces = 0x0000FC00;
		constexpr uint32_t ddsCaps2Volume = 0x00200000;
		constexpr uint32_t ddsResourceMiscTextureCube = 0x4;

		constexpr uint32_t FourCc(char a, char b, char c, char d)
		{
			return static_cast<uint8_t>(a) |
				(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
				(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
				(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
		}

		constexpr uint32_t fourCcDx10 = FourCc('D', 'X', '1', '0');
		constexpr uint32_t fourCcDxt1 = FourCc('D', 'X', 'T', '1');
		constexpr uint32_t fourCcDxt3 = FourCc('D', 'X', 'T', '3');
		constexpr uint32_t fourCcDxt5 = FourCc('D', 'X', 'T', '5');
		constexpr uint32_t fourCcAti1 = FourCc('A', 'T', 'I', '1');
		constexpr uint32_t fourCcAti2 = FourCc('A', 'T', 'I', '2');

#pragma pack(push, 1)
		struct DdsPixelFormat
		{
			uint32_t size;
			uint32_t flags;
			uint32_t fourCC;
			uint32_t rgbBitCount;
			uint32_t redMask;
			uint32_t greenMask;
			uint32_t blueMask;
			uint32_t alphaMask;
		};

		struct DdsHeader
		{
			uint32_t size;
			uint32_t flags;
			uint32_t height;
			uint32_t width;
			uint32_t pitchOrLinearSize;
			uint32_t depth;
			uint32_t mipMapCount;
			uint32_t reserved1[11];
			DdsPixelFormat pixelFormat;
			uint32_t caps;
			uint32_t caps2;
			uint32_t caps3;
			uint32_t caps4;
			uint32_t reserved2;
		};

		struct DdsHeaderDx10
		{
			DXGI_FORMAT format;
			uint32_t resourceDimension;
			uint32_t miscFlag;
			uint32_t arraySize;
			uint32_t miscFlags2;
		};
#pragma pack(pop)

		static_assert(sizeof(DdsPixelFormat) == 32, "Unexpected DDS pixel format size.");
		static_assert(sizeof(DdsHeader) == 124, "Unexpected DDS header size.");
		static_assert(sizeof(DdsHeaderDx10) == 20, "Unexpected DDS DX10 header size.");

		DXGI_FORMAT LegacyFormat(const DdsPixelFormat& format)
		{
			if (format.flags & fourCcFlag)
			{
				switch (format.fourCC)
				{
					case fourCcDxt1: return DXGI_FORMAT_BC1_UNORM;
					case fourCcDxt3: return DXGI_FORMAT_BC2_UNORM;
					case fourCcDxt5: return DXGI_FORMAT_BC3_UNORM;
					case fourCcAti1: return DXGI_FORMAT_BC4_UNORM;
					case fourCcAti2: return DXGI_FORMAT_BC5_UNORM;
				}
			}

			if ((format.flags & rgbFlag) && format.rgbBitCount == 32)
			{
				if (format.redMask == 0x000000ff && format.greenMask == 0x0000ff00 &&
					format.blueMask == 0x00ff0000)
				{
					return DXGI_FORMAT_R8G8B8A8_UNORM;
				}
				if (format.redMask == 0x00ff0000 && format.greenMask == 0x0000ff00 &&
					format.blueMask == 0x000000ff)
				{
					return DXGI_FORMAT_B8G8R8A8_UNORM;
				}
			}

			return DXGI_FORMAT_UNKNOWN;
		}

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

	bool ReadMetadata(const std::string& filePath, Metadata& outMetadata, std::string& outError)
	{
		outMetadata = {};
		std::ifstream file;
		std::streamoff pixelOffset = 0;
		if (!OpenAndReadHeader(filePath, file, outMetadata, pixelOffset, outError))
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
		if (!OpenAndReadHeader(filePath, file, outImage.metadata, pixelOffset, outError))
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
