
#include "../HookD3D12.h"
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

		if (IsInsideRenderPassInjection() || RenderPassResourceRegistry::IsInsideDescriptorMirrorOperation())
			return;

		const bool mirrorHeapType = heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

		const bool trackMetadata = ShouldTrackRenderPassDescriptorMetadata() && (mirrorHeapType || ShouldTrackRenderPassResourceMetadata());

		const bool trackMirrors = mirrorHeapType && RenderPassResourceRegistry::IsDescriptorMirroringActive();

		if (!trackMetadata && !trackMirrors)
			return;

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopySimple);
		PerformanceMetrics::ScopedTimer descriptorCopyTimer(PerformanceMetrics::Timing::DescriptorCopySimplePropagation, 256);

		const bool inspectedRegistry = RenderPassResourceRegistry::CopyDescriptorsSimple(
			descriptorCount,
			destinationStart,
			sourceStart,
			DescriptorIncrementSize(device, heapType), device, trackMetadata);

		if (inspectedRegistry)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopyRegistryHit);
	}
} //namespace HookD3D12
