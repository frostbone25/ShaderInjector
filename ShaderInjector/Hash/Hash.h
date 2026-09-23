#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Hash
{
	uint64_t HashMemory(const void* data, size_t size);

	std::string FormatHash(uint64_t hash);

	uint64_t ParseHashText(const std::string& hashText);
}
