#include "../HookD3D12.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle)
	{
		Handle_SetComputeRootDescriptorTable(commandList, rootParameterIndex, descriptorHandle);
	}

	void STDMETHODCALLTYPE Handle_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsDescriptorTableTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(true) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootDescriptorTable(commandList, true, rootParameterIndex, descriptorHandle);
		}

		Original_SetComputeRootDescriptorTable(commandList, rootParameterIndex, descriptorHandle);
	}
}
