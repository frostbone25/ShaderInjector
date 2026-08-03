#include "HookD3D12Resources.h"

#include "HookD3D12RenderPass.h"
#include "RenderPassResourceRegistry.h"
#include "RenderPassRuntime.h"

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

	void STDMETHODCALLTYPE Hook_CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateConstantBufferView(device, description, destination);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterConstantBufferView(description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateShaderResourceView(device, resource, description, destination);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::HasEnabledMipChainPasses())
		{
			RenderPassResourceRegistry::RegisterShaderResourceView(
				resource,
				description,
				destination,
				RenderPassRuntime::IsResourceTrackingRequired());
		}
	}

	void STDMETHODCALLTYPE Hook_CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateUnorderedAccessView(device, resource, counterResource, description, destination);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterUnorderedAccessView(resource, counterResource, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateRenderTargetView(device, resource, description, destination);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterRenderTargetView(resource, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateDepthStencilView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateDepthStencilView(device, resource, description, destination);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterDepthStencilView(resource, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateSampler(device, description, destination);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterSampler(destination);
	}

	void STDMETHODCALLTYPE Hook_CopyDescriptors(ID3D12Device* device, UINT destinationRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts, const UINT* destinationRangeSizes, UINT sourceRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts, const UINT* sourceRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType)
	{
		Original_CopyDescriptors(device, destinationRangeCount, destinationRangeStarts, destinationRangeSizes, sourceRangeCount, sourceRangeStarts, sourceRangeSizes, descriptorHeapType);
		if (IsInsideRenderPassInjection() ||
			!RenderPassRuntime::HasEnabledMipChainPasses() ||
			(descriptorHeapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				!RenderPassRuntime::IsResourceTrackingRequired()))
		{
			return;
		}
		RenderPassResourceRegistry::CopyDescriptors(
			destinationRangeCount,
			destinationRangeStarts,
			destinationRangeSizes,
			sourceRangeCount,
			sourceRangeStarts,
			sourceRangeSizes,
			device->GetDescriptorHandleIncrementSize(descriptorHeapType));
	}

	void STDMETHODCALLTYPE Hook_CopyDescriptorsSimple(ID3D12Device* device, UINT descriptorCount, D3D12_CPU_DESCRIPTOR_HANDLE destinationStart, D3D12_CPU_DESCRIPTOR_HANDLE sourceStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType)
	{
		Original_CopyDescriptorsSimple(device, descriptorCount, destinationStart, sourceStart, descriptorHeapType);
		if (IsInsideRenderPassInjection() ||
			!RenderPassRuntime::HasEnabledMipChainPasses() ||
			(descriptorHeapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				!RenderPassRuntime::IsResourceTrackingRequired()))
		{
			return;
		}
		RenderPassResourceRegistry::CopyDescriptorsSimple(
			descriptorCount,
			destinationStart,
			sourceStart,
			device->GetDescriptorHandleIncrementSize(descriptorHeapType));
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heapProperties, D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** resource)
	{
		const HRESULT result = Original_CreateCommittedResource(device, heapProperties, heapFlags, description, initialState, clearValue, riid, resource);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RegisterCreatedResource(result, resource);
		return result;
	}

	HRESULT STDMETHODCALLTYPE Hook_CreatePlacedResource(ID3D12Device* device, ID3D12Heap* heap, UINT64 heapOffset, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** resource)
	{
		const HRESULT result = Original_CreatePlacedResource(device, heap, heapOffset, description, initialState, clearValue, riid, resource);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RegisterCreatedResource(result, resource);
		return result;
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** resource)
	{
		const HRESULT result = Original_CreateReservedResource(device, description, initialState, clearValue, riid, resource);
		if (!IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RegisterCreatedResource(result, resource);
		return result;
	}
}
