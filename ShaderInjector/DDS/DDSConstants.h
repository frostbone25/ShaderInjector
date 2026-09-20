#pragma once

#include <cstdint>

namespace DDS::Internal
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
}
