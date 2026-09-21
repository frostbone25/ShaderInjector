#include "../HookD3D12.h"

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateUnorderedAccessView(device, resource, counterResource, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateUnorderedAccessView(device, resource, counterResource, description, destination);

		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsDescriptorRegistryTrackingRequired())
			RenderPassResourceRegistry::RegisterUnorderedAccessView(resource, counterResource, description, destination);
	}
}
