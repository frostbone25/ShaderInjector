#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, UINT descriptorHeapCount, ID3D12DescriptorHeap* const* descriptorHeaps)
	{
		Handle_SetDescriptorHeaps(commandList, descriptorHeapCount, descriptorHeaps);
	}

	void STDMETHODCALLTYPE Handle_SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, UINT descriptorHeapCount, ID3D12DescriptorHeap* const* descriptorHeaps)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsDescriptorTableTrackingRequired() &&
			(RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) || RenderPassRuntime::IsPipelineExecutionTrackingRequired(true)) &&
			!IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackDescriptorHeaps(commandList, descriptorHeapCount, descriptorHeaps);
		}
		Original_SetDescriptorHeaps(commandList, descriptorHeapCount, descriptorHeaps);
	}
}
