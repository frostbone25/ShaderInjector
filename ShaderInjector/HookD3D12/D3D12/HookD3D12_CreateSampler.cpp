
#include "../HookD3D12.h"
#include "Globals.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateSampler(device, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateSampler(device, description, destination);

		if (Globals::gShaderInjectorEnabled &&
			!IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterSampler(destination);
	}
} //namespace HookD3D12
