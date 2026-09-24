
#include "../HookD3D12.h"
#include "Globals.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateConstantBufferView(device, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateConstantBufferView(device, description, destination);

		if (Globals::gShaderInjectorEnabled &&
			!IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterConstantBufferView(description, destination);
	}
} //namespace HookD3D12
