#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT valueCount, const void* values, UINT destinationOffset)
	{
		Handle_SetComputeRoot32BitConstants(commandList, rootParameterIndex, valueCount, values, destinationOffset);
	}

	void STDMETHODCALLTYPE Handle_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT valueCount, const void* values, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsRootBindingTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(true) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootConstants(commandList, true, rootParameterIndex, valueCount, values, destinationOffset);
		}

		Original_SetComputeRoot32BitConstants(commandList, rootParameterIndex, valueCount, values, destinationOffset);
	}
}
