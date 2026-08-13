#include "HookD3D12Resources.h"

#include <array>

#include "HookD3D12RenderPass.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPassResourceRegistry.h"
#include "RenderPassRuntime.h"

namespace HookD3D12
{
	namespace
	{
		UINT DescriptorIncrementSize(
			ID3D12Device* device,
			D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType)
		{
			if (!device || descriptorHeapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES)
				return 0;
			struct ThreadDescriptorIncrements
			{
				ID3D12Device* device = nullptr;
				std::array<UINT, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> values{};
			};
			thread_local ThreadDescriptorIncrements cache;
			if (cache.device != device)
			{
				cache.device = device;
				cache.values.fill(0);
			}

			UINT& increment = cache.values[descriptorHeapType];
			if (!increment)
				increment = device->GetDescriptorHandleIncrementSize(descriptorHeapType);
			return increment;
		}

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

	HRESULT STDMETHODCALLTYPE Hook_CreateDescriptorHeap(
		ID3D12Device* device,
		const D3D12_DESCRIPTOR_HEAP_DESC* description,
		REFIID riid,
		void** descriptorHeapObject)
	{
		const HRESULT result = Original_CreateDescriptorHeap(
			device,
			description,
			riid,
			descriptorHeapObject);
		if (FAILED(result) || !device || !description || !descriptorHeapObject ||
			!*descriptorHeapObject || IsInsideRenderPassInjection() ||
			!RenderPassRuntime::IsDescriptorRegistryTrackingRequired())
		{
			return result;
		}

		ID3D12DescriptorHeap* descriptorHeap = nullptr;
		IUnknown* unknown = reinterpret_cast<IUnknown*>(*descriptorHeapObject);
		if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&descriptorHeap))) && descriptorHeap)
		{
			RenderPassResourceRegistry::RegisterDescriptorHeap(
				descriptorHeap,
				DescriptorIncrementSize(device, description->Type),
				true);
			descriptorHeap->Release();
		}
		return result;
	}

	void STDMETHODCALLTYPE Hook_CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateConstantBufferView(device, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RenderPassResourceRegistry::RegisterConstantBufferView(description, destination);
		}
	}

	void STDMETHODCALLTYPE Hook_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateShaderResourceView(device, resource, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsDescriptorRegistryTrackingRequired())
		{
			PerformanceMetrics::ScopedTimer registrationTimer(
				PerformanceMetrics::Timing::RegisterShaderResourceView,
				64);
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
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RenderPassResourceRegistry::RegisterUnorderedAccessView(resource, counterResource, description, destination);
		}
	}

	void STDMETHODCALLTYPE Hook_CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateRenderTargetView(device, resource, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RenderPassResourceRegistry::RegisterRenderTargetView(resource, description, destination);
		}
	}

	void STDMETHODCALLTYPE Hook_CreateDepthStencilView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateDepthStencilView(device, resource, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RenderPassResourceRegistry::RegisterDepthStencilView(resource, description, destination);
		}
	}

	void STDMETHODCALLTYPE Hook_CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateSampler(device, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RenderPassResourceRegistry::RegisterSampler(destination);
		}
	}

	void STDMETHODCALLTYPE Hook_CopyDescriptors(ID3D12Device* device, UINT destinationRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts, const UINT* destinationRangeSizes, UINT sourceRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts, const UINT* sourceRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType)
	{
		Original_CopyDescriptors(device, destinationRangeCount, destinationRangeStarts, destinationRangeSizes, sourceRangeCount, sourceRangeStarts, sourceRangeSizes, descriptorHeapType);
		if (!Globals::gShaderInjectorEnabled ||
			IsInsideRenderPassInjection() ||
			!RenderPassRuntime::IsDescriptorRegistryTrackingRequired() ||
			(descriptorHeapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				!RenderPassRuntime::IsResourceTrackingRequired()))
		{
			return;
		}
		PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopy);
		PerformanceMetrics::ScopedTimer descriptorCopyTimer(
			PerformanceMetrics::Timing::DescriptorCopyPropagation,
			256);
		const bool inspectedRegistry = RenderPassResourceRegistry::CopyDescriptors(
			destinationRangeCount,
			destinationRangeStarts,
			destinationRangeSizes,
			sourceRangeCount,
			sourceRangeStarts,
			sourceRangeSizes,
			DescriptorIncrementSize(device, descriptorHeapType));
		if (inspectedRegistry)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopyRegistryHit);
	}

	void STDMETHODCALLTYPE Hook_CopyDescriptorsSimple(ID3D12Device* device, UINT descriptorCount, D3D12_CPU_DESCRIPTOR_HANDLE destinationStart, D3D12_CPU_DESCRIPTOR_HANDLE sourceStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType)
	{
		Original_CopyDescriptorsSimple(device, descriptorCount, destinationStart, sourceStart, descriptorHeapType);
		if (!Globals::gShaderInjectorEnabled ||
			IsInsideRenderPassInjection() ||
			!RenderPassRuntime::IsDescriptorRegistryTrackingRequired() ||
			(descriptorHeapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				!RenderPassRuntime::IsResourceTrackingRequired()))
		{
			return;
		}
		PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopySimple);
		PerformanceMetrics::ScopedTimer descriptorCopyTimer(
			PerformanceMetrics::Timing::DescriptorCopySimplePropagation,
			256);
		const bool inspectedRegistry = RenderPassResourceRegistry::CopyDescriptorsSimple(
			descriptorCount,
			destinationStart,
			sourceStart,
			DescriptorIncrementSize(device, descriptorHeapType));
		if (inspectedRegistry)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::DescriptorCopyRegistryHit);
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heapProperties, D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** resource)
	{
		const HRESULT result = Original_CreateCommittedResource(device, heapProperties, heapFlags, description, initialState, clearValue, riid, resource);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RegisterCreatedResource(result, resource);
		}
		return result;
	}

	HRESULT STDMETHODCALLTYPE Hook_CreatePlacedResource(ID3D12Device* device, ID3D12Heap* heap, UINT64 heapOffset, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** resource)
	{
		const HRESULT result = Original_CreatePlacedResource(device, heap, heapOffset, description, initialState, clearValue, riid, resource);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RegisterCreatedResource(result, resource);
		}
		return result;
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** resource)
	{
		const HRESULT result = Original_CreateReservedResource(device, description, initialState, clearValue, riid, resource);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
		{
			RegisterCreatedResource(result, resource);
		}
		return result;
	}
}
