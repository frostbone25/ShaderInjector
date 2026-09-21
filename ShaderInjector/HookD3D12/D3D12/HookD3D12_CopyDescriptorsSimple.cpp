#include "HookD3D12HookHandlers.h"

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CopyDescriptorsSimple(ID3D12Device* device, UINT descriptorCount, D3D12_CPU_DESCRIPTOR_HANDLE destinationStart, D3D12_CPU_DESCRIPTOR_HANDLE sourceStart, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Handle_CopyDescriptorsSimple(device, descriptorCount, destinationStart, sourceStart, heapType);
	}

	void STDMETHODCALLTYPE Handle_CopyDescriptorsSimple(ID3D12Device* device, UINT descriptorCount, D3D12_CPU_DESCRIPTOR_HANDLE destinationStart, D3D12_CPU_DESCRIPTOR_HANDLE sourceStart, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		Original_CopyDescriptorsSimple(device, descriptorCount, destinationStart, sourceStart, heapType);

		if (!Globals::gShaderInjectorEnabled ||
			IsInsideRenderPassInjection() ||
			!RenderPassRuntime::IsDescriptorRegistryTrackingRequired() ||
			(heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			!RenderPassRuntime::IsResourceTrackingRequired()))
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
