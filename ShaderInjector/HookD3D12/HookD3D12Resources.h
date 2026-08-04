#pragma once

#include <d3d12.h>

namespace HookD3D12
{
	using FunctionCreateConstantBufferViewD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using FunctionCreateShaderResourceViewD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using FunctionCreateUnorderedAccessViewD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using FunctionCreateRenderTargetViewD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using FunctionCreateDepthStencilViewD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using FunctionCreateSamplerD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using FunctionCopyDescriptorsD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	using FunctionCopyDescriptorsSimpleD3D12 = void(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
	using FunctionCreateCommittedResourceD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using FunctionCreatePlacedResourceD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using FunctionCreateReservedResourceD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);

	extern FunctionCreateConstantBufferViewD3D12 Original_CreateConstantBufferView;
	extern FunctionCreateShaderResourceViewD3D12 Original_CreateShaderResourceView;
	extern FunctionCreateUnorderedAccessViewD3D12 Original_CreateUnorderedAccessView;
	extern FunctionCreateRenderTargetViewD3D12 Original_CreateRenderTargetView;
	extern FunctionCreateDepthStencilViewD3D12 Original_CreateDepthStencilView;
	extern FunctionCreateSamplerD3D12 Original_CreateSampler;
	extern FunctionCopyDescriptorsD3D12 Original_CopyDescriptors;
	extern FunctionCopyDescriptorsSimpleD3D12 Original_CopyDescriptorsSimple;
	extern FunctionCreateCommittedResourceD3D12 Original_CreateCommittedResource;
	extern FunctionCreatePlacedResourceD3D12 Original_CreatePlacedResource;
	extern FunctionCreateReservedResourceD3D12 Original_CreateReservedResource;

	void STDMETHODCALLTYPE Hook_CreateConstantBufferView(ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateShaderResourceView(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateUnorderedAccessView(ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateRenderTargetView(ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateDepthStencilView(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateSampler(ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CopyDescriptors(ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	void STDMETHODCALLTYPE Hook_CopyDescriptorsSimple(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
	HRESULT STDMETHODCALLTYPE Hook_CreateCommittedResource(ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Hook_CreatePlacedResource(ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Hook_CreateReservedResource(ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
}
