#include "HookD3D12HookHandlers.h"

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CopyDescriptors(ID3D12Device* device, UINT destinationRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts, const UINT* destinationRangeSizes, UINT sourceRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts, const UINT* sourceRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Handle_CopyDescriptors(device, destinationRangeCount, destinationRangeStarts, destinationRangeSizes, sourceRangeCount, sourceRangeStarts, sourceRangeSizes, heapType);
	}

	void STDMETHODCALLTYPE Handle_CopyDescriptors(ID3D12Device* device, UINT destinationRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts, const UINT* destinationRangeSizes, UINT sourceRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts, const UINT* sourceRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Original_CopyDescriptors(device, destinationRangeCount, destinationRangeStarts, destinationRangeSizes, sourceRangeCount, sourceRangeStarts, sourceRangeSizes, heapType);

		if (!Globals::gShaderInjectorEnabled || 
			IsInsideRenderPassInjection() ||
			!RenderPassRuntime::IsDescriptorRegistryTrackingRequired() ||
			(heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && 
			!RenderPassRuntime::IsResourceTrackingRequired()))
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
}
