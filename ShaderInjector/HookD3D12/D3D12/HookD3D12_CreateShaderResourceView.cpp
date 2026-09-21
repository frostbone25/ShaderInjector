#include "../HookD3D12.h"

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateShaderResourceView(device, resource, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateShaderResourceView(device, resource, description, destination);

		if (Globals::gShaderInjectorEnabled && 
			!IsInsideRenderPassInjection() && 
			RenderPassRuntime::IsDescriptorRegistryTrackingRequired())
		{
			PerformanceMetrics::ScopedTimer registrationTimer(PerformanceMetrics::Timing::RegisterShaderResourceView, 64);

			RenderPassResourceRegistry::RegisterShaderResourceView(
				resource,
				description,
				destination,
				RenderPassRuntime::IsResourceTrackingRequired() || RenderPassRuntime::IsGameTextureDescriptorTrackingRequired());
		}
	}
}
