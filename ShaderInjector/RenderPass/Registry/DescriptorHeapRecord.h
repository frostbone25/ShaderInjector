#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include <d3d12.h>

#include "RenderPass/Registry/DescriptorPage.h"

namespace RenderPassResourceRegistry
{
	//index a game's descriptor heap and own the metadata pages attached to it.
	struct DescriptorHeapRecord
	{
		ID3D12DescriptorHeap* identity = nullptr;
		SIZE_T start = 0;
		SIZE_T end = 0;
		UINT descriptorCount = 0;
		UINT descriptorIncrementSize = 0;
		D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		std::unique_ptr<std::atomic<DescriptorPage*>[]> pages;
		size_t pageCount = 0;
		std::atomic<size_t> trackedDescriptorCount{0};
		std::atomic<bool> active{true};

		~DescriptorHeapRecord()
		{
			if (!pages)
				return;
			for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
				delete pages[pageIndex].load(std::memory_order_relaxed);
		}
	};
} //namespace RenderPassResourceRegistry
