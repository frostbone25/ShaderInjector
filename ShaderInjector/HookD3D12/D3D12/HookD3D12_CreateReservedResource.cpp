#include "../HookD3D12.h"

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	HRESULT STDMETHODCALLTYPE Hook_CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		return Handle_CreateReservedResource(device, description, initialState, clearValue, interfaceId, resource);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		const HRESULT result = Original_CreateReservedResource(device, description, initialState, clearValue, interfaceId, resource);

		if (Globals::gShaderInjectorEnabled && 
			!IsInsideRenderPassInjection() && 
			RenderPassRuntime::IsResourceTrackingRequired())
			RegisterCreatedResource(result, resource);

		return result;
	}
}
