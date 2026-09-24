#include "../HookD3D12.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetGraphicsRootConstantBufferView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Handle_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) &&
			RenderPassRuntime::IsRootBindingTrackingRequired() && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackRootDescriptor(commandList, false, "CBV", rootParameterIndex, address);
		}

		Original_SetGraphicsRootConstantBufferView(commandList, rootParameterIndex, address);
	}
} //namespace HookD3D12
