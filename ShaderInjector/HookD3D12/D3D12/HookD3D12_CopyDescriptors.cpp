#include "HookD3D12HookHandlers.h"

#include <array>

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	namespace
	{
		UINT DescriptorIncrementSize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
		{
			if (!device || heapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES)
				return 0;

			struct ThreadDescriptorIncrements
			{
				ID3D12Device* device = nullptr;
				std::array<UINT, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> values{};
			};
			thread_local ThreadDescriptorIncrements cache;
			if (cache.device != device)
			{
				cache.device = device;
				cache.values.fill(0);
			}

			UINT& increment = cache.values[heapType];
			if (!increment)
				increment = device->GetDescriptorHandleIncrementSize(heapType);
			return increment;
		}
	}

	void STDMETHODCALLTYPE Hook_CopyDescriptors(ID3D12Device* device, UINT destinationRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts, const UINT* destinationRangeSizes, UINT sourceRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts, const UINT* sourceRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Handle_CopyDescriptors(device, destinationRangeCount, destinationRangeStarts, destinationRangeSizes, sourceRangeCount, sourceRangeStarts, sourceRangeSizes, heapType);
	}

	void STDMETHODCALLTYPE Hook_CopyDescriptorsSimple(ID3D12Device* device, UINT descriptorCount, D3D12_CPU_DESCRIPTOR_HANDLE destinationStart, D3D12_CPU_DESCRIPTOR_HANDLE sourceStart, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Handle_CopyDescriptorsSimple(device, descriptorCount, destinationStart, sourceStart, heapType);
	}

	void STDMETHODCALLTYPE Handle_CopyDescriptors(ID3D12Device* device, UINT destinationRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts, const UINT* destinationRangeSizes, UINT sourceRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts, const UINT* sourceRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Original_CopyDescriptors(device, destinationRangeCount, destinationRangeStarts, destinationRangeSizes, sourceRangeCount, sourceRangeStarts, sourceRangeSizes, heapType);
		if (!Globals::gShaderInjectorEnabled || IsInsideRenderPassInjection() ||
			!RenderPassRuntime::IsDescriptorRegistryTrackingRequired() ||
			(heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && !RenderPassRuntime::IsResourceTrackingRequired()))
		{
			return;
		}

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopy);
		PerformanceMetrics::ScopedTimer descriptorCopyTimer(PerformanceMetrics::Timing::DescriptorCopyPropagation, 256);
		const bool inspectedRegistry = RenderPassResourceRegistry::CopyDescriptors(
			destinationRangeCount,
			destinationRangeStarts,
			destinationRangeSizes,
			sourceRangeCount,
			sourceRangeStarts,
			sourceRangeSizes,
			DescriptorIncrementSize(device, heapType));
		if (inspectedRegistry)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopyRegistryHit);
	}

	void STDMETHODCALLTYPE Handle_CopyDescriptorsSimple(ID3D12Device* device, UINT descriptorCount, D3D12_CPU_DESCRIPTOR_HANDLE destinationStart, D3D12_CPU_DESCRIPTOR_HANDLE sourceStart, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Original_CopyDescriptorsSimple(device, descriptorCount, destinationStart, sourceStart, heapType);
		if (!Globals::gShaderInjectorEnabled || IsInsideRenderPassInjection() ||
			!RenderPassRuntime::IsDescriptorRegistryTrackingRequired() ||
			(heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && !RenderPassRuntime::IsResourceTrackingRequired()))
		{
			return;
		}

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopySimple);
		PerformanceMetrics::ScopedTimer descriptorCopyTimer(PerformanceMetrics::Timing::DescriptorCopySimplePropagation, 256);
		const bool inspectedRegistry = RenderPassResourceRegistry::CopyDescriptorsSimple(
			descriptorCount,
			destinationStart,
			sourceStart,
			DescriptorIncrementSize(device, heapType));
		if (inspectedRegistry)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopyRegistryHit);
	}
}
