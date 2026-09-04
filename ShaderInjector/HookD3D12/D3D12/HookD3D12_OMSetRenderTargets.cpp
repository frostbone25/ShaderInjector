#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_OMSetRenderTargets(ID3D12GraphicsCommandList* commandList, UINT renderTargetCount, const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors, BOOL descriptorsAreContiguous, const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor)
	{
		Handle_OMSetRenderTargets(commandList, renderTargetCount, renderTargetDescriptors, descriptorsAreContiguous, depthStencilDescriptor);
	}

	void STDMETHODCALLTYPE Handle_OMSetRenderTargets(ID3D12GraphicsCommandList* commandList, UINT renderTargetCount, const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors, BOOL descriptorsAreContiguous, const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsGraphicsStateTrackingRequired())
		{
			RenderPassRuntime::TrackRenderTargets(commandList, renderTargetCount, renderTargetDescriptors, descriptorsAreContiguous, depthStencilDescriptor);
		}
		Original_OMSetRenderTargets(commandList, renderTargetCount, renderTargetDescriptors, descriptorsAreContiguous, depthStencilDescriptor);
	}
}
