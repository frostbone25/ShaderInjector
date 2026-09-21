#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		Handle_SetComputeRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}

	void STDMETHODCALLTYPE Handle_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsRootBindingTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(true) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootConstants(commandList, true, rootParameterIndex, 1, &value, destinationOffset);
		}

		Original_SetComputeRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}
}
