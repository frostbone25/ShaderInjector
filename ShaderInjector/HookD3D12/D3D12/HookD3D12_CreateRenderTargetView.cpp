
#include "../HookD3D12.h"
#include "Globals.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateRenderTargetView(device, resource, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateRenderTargetView(device, resource, description, destination);

		if (Globals::gShaderInjectorEnabled && 
			!IsInsideRenderPassInjection() && 
			RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterRenderTargetView(resource, description, destination);
	}
}
