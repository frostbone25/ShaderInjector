#include "../HookD3D12.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		Handle_SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}

	void STDMETHODCALLTYPE Handle_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsRootBindingTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootConstants(commandList, false, rootParameterIndex, 1, &value, destinationOffset);
		}

		Original_SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}
} //namespace HookD3D12
