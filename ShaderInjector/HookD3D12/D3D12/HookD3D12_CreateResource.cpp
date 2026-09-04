#include "HookD3D12HookHandlers.h"

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	namespace
	{
		void RegisterCreatedResource(HRESULT result, void** createdObject)
		{
			if (FAILED(result) || !createdObject || !*createdObject)
				return;

			ID3D12Resource* resource = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*createdObject);
			if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&resource))) && resource)
			{
				RenderPassResourceRegistry::RegisterResource(resource);
				resource->Release();
			}
		}
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heapProperties, D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		return Handle_CreateCommittedResource(device, heapProperties, heapFlags, description, initialState, clearValue, interfaceId, resource);
	}

	HRESULT STDMETHODCALLTYPE Hook_CreatePlacedResource(ID3D12Device* device, ID3D12Heap* heap, UINT64 heapOffset, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		return Handle_CreatePlacedResource(device, heap, heapOffset, description, initialState, clearValue, interfaceId, resource);
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		return Handle_CreateReservedResource(device, description, initialState, clearValue, interfaceId, resource);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heapProperties, D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		const HRESULT result = Original_CreateCommittedResource(device, heapProperties, heapFlags, description, initialState, clearValue, interfaceId, resource);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RegisterCreatedResource(result, resource);
		return result;
	}

	HRESULT STDMETHODCALLTYPE Handle_CreatePlacedResource(ID3D12Device* device, ID3D12Heap* heap, UINT64 heapOffset, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		const HRESULT result = Original_CreatePlacedResource(device, heap, heapOffset, description, initialState, clearValue, interfaceId, resource);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RegisterCreatedResource(result, resource);
		return result;
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID interfaceId, void** resource)
	{
		const HRESULT result = Original_CreateReservedResource(device, description, initialState, clearValue, interfaceId, resource);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RegisterCreatedResource(result, resource);
		return result;
	}
}
