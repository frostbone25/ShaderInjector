#pragma once

#include <cstddef>
#include <memory>

#include <d3d12.h>

#include "RenderPass/Registry/DescriptorHeapRecord.h"

namespace RenderPassResourceRegistry
{
	//map one copy segment to its source and destination heap pages.
	struct DescriptorCopySegment
	{
		SIZE_T destinationStart = 0;
		SIZE_T sourceStart = 0;
		UINT descriptorCount = 0;
		std::shared_ptr<DescriptorHeapRecord> destinationHeap;
		std::shared_ptr<DescriptorHeapRecord> sourceHeap;
		SIZE_T destinationFirstDescriptor = 0;
		SIZE_T sourceFirstDescriptor = 0;
	};
} //namespace RenderPassResourceRegistry
