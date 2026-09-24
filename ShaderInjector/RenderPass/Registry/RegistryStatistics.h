#pragma once

#include <cstddef>

namespace RenderPassResourceRegistry
{
	//summarize tracked descriptor and resource counts for diagnostics.
	struct RegistryStatistics
	{
		size_t descriptorCount = 0;
		size_t descriptorHeapCount = 0;
		size_t retiredDescriptorHeapCount = 0;
		size_t heapDescriptorCount = 0;
		size_t fallbackDescriptorCount = 0;
		size_t descriptorMetadataCount = 0;
		size_t bufferResourceCount = 0;
		size_t rootSignatureCount = 0;
	};
} //namespace RenderPassResourceRegistry
