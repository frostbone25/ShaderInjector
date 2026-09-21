#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetGraphicsRootUnorderedAccessView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Handle_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) &&
			RenderPassRuntime::IsRootBindingTrackingRequired() && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootDescriptor(commandList, false, "UAV", rootParameterIndex, address);
		}

		Original_SetGraphicsRootUnorderedAccessView(commandList, rootParameterIndex, address);
	}
}
