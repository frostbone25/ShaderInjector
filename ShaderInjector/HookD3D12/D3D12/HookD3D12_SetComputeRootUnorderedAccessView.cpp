#include "../HookD3D12.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetComputeRootUnorderedAccessView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Handle_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsPipelineExecutionTrackingRequired(true) &&
			RenderPassRuntime::IsRootBindingTrackingRequired() && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootDescriptor(commandList, true, "UAV", rootParameterIndex, address);
		}

		Original_SetComputeRootUnorderedAccessView(commandList, rootParameterIndex, address);
	}
} //namespace HookD3D12
