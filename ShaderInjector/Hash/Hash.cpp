#include "Hash.h"

#include <cstdio>
#include <cstdlib>

namespace Hash
{
	uint64_t HashMemory(const void* data, size_t size)
	{
		const uint8_t* dataBytes = (const uint8_t*)data;
		uint64_t hash = 14695981039346656037ULL;

		for (size_t byteIndex = 0; byteIndex < size; ++byteIndex)
		{
			hash ^= dataBytes[byteIndex];
			hash *= 1099511628211ULL;
		}

		return hash;
	}

	std::string FormatHash(uint64_t hash)
	{
		char buffer[32]{};
		sprintf_s(buffer, "%016llX", (unsigned long long)hash);
		return buffer;
	}

	uint64_t ParseHashText(const std::string& hashText)
	{
		if (hashText.empty())
			return 0;

		char* parseEnd = nullptr;
		return strtoull(hashText.c_str(), &parseEnd, 16);
	}
} //namespace Hash
