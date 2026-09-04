#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		Handle_SetComputeRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		Handle_SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT valueCount, const void* values, UINT destinationOffset)
	{
		Handle_SetComputeRoot32BitConstants(commandList, rootParameterIndex, valueCount, values, destinationOffset);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT valueCount, const void* values, UINT destinationOffset)
	{
		Handle_SetGraphicsRoot32BitConstants(commandList, rootParameterIndex, valueCount, values, destinationOffset);
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

	void STDMETHODCALLTYPE Handle_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsRootBindingTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootConstants(commandList, false, rootParameterIndex, 1, &value, destinationOffset);
		}
		Original_SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
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

	void STDMETHODCALLTYPE Handle_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT valueCount, const void* values, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsRootBindingTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootConstants(commandList, false, rootParameterIndex, valueCount, values, destinationOffset);
		}
		Original_SetGraphicsRoot32BitConstants(commandList, rootParameterIndex, valueCount, values, destinationOffset);
	}
}
