#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "RenderPass/Registry/DescriptorHeapRecord.h"

namespace RenderPassResourceRegistry
{
	//keep recent heap lookups per thread while the registry generation is unchanged.
	struct DescriptorHeapLookupCache
	{
		uint64_t generation = 0;
		size_t nextEntry = 0;
		std::array<std::shared_ptr<DescriptorHeapRecord>, 4> entries{};
	};
} //namespace RenderPassResourceRegistry
