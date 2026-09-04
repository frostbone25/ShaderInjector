#include "HookD3D12HookHandlers.h"

#include <array>

#include "../HookD3D12RenderPass.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	HRESULT STDMETHODCALLTYPE Hook_CreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* description, REFIID interfaceId, void** descriptorHeap)
	{
		return Handle_CreateDescriptorHeap(device, description, interfaceId, descriptorHeap);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* description, REFIID interfaceId, void** descriptorHeapObject)
	{
		const HRESULT result = Original_CreateDescriptorHeap(device, description, interfaceId, descriptorHeapObject);
		if (FAILED(result) || !device || !description || !descriptorHeapObject || !*descriptorHeapObject ||
			IsInsideRenderPassInjection() || !RenderPassRuntime::IsDescriptorRegistryTrackingRequired())
		{
			return result;
		}

		ID3D12DescriptorHeap* descriptorHeap = nullptr;
		IUnknown* unknown = reinterpret_cast<IUnknown*>(*descriptorHeapObject);
		if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&descriptorHeap))) && descriptorHeap)
		{
			const UINT descriptorIncrement = device->GetDescriptorHandleIncrementSize(description->Type);
			RenderPassResourceRegistry::RegisterDescriptorHeap(descriptorHeap, descriptorIncrement, true);
			descriptorHeap->Release();
		}
		return result;
	}
}
