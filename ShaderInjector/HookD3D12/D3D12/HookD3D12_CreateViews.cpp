#include "HookD3D12HookHandlers.h"

#include "../HookD3D12RenderPass.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateConstantBufferView(device, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateShaderResourceView(device, resource, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateUnorderedAccessView(device, resource, counterResource, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateRenderTargetView(device, resource, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateDepthStencilView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateDepthStencilView(device, resource, description, destination);
	}

	void STDMETHODCALLTYPE Hook_CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Handle_CreateSampler(device, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateConstantBufferView(device, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterConstantBufferView(description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateShaderResourceView(device, resource, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsDescriptorRegistryTrackingRequired())
		{
			PerformanceMetrics::ScopedTimer registrationTimer(PerformanceMetrics::Timing::RegisterShaderResourceView, 64);
			RenderPassResourceRegistry::RegisterShaderResourceView(
				resource,
				description,
				destination,
				RenderPassRuntime::IsResourceTrackingRequired() || RenderPassRuntime::IsGameTextureDescriptorTrackingRequired());
		}
	}

	void STDMETHODCALLTYPE Handle_CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateUnorderedAccessView(device, resource, counterResource, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsDescriptorRegistryTrackingRequired())
			RenderPassResourceRegistry::RegisterUnorderedAccessView(resource, counterResource, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateRenderTargetView(device, resource, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterRenderTargetView(resource, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateDepthStencilView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateDepthStencilView(device, resource, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterDepthStencilView(resource, description, destination);
	}

	void STDMETHODCALLTYPE Handle_CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		Original_CreateSampler(device, description, destination);
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassResourceRegistry::RegisterSampler(destination);
	}
}
