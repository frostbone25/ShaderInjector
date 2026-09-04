#pragma once

#include "../HookD3D12.h"
#include "../HookD3D12RenderPass.h"
#include "../HookD3D12Resources.h"

namespace HookD3D12
{
	// Hook entry points live in this directory. Their implementation handlers stay
	// grouped by subsystem so hook installation is decoupled from runtime state.
	HRESULT WINAPI Handle_CreateDeviceD3D12(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

	HRESULT STDMETHODCALLTYPE Handle_CreateComputePipelineState(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreateGraphicsPipelineState(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreatePipelineState(ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreateRootSignature(ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);
	HRESULT __stdcall Handle_CreatePipelineLibrary(ID3D12Device1*, const void*, SIZE_T, REFIID, void**);

	HRESULT __stdcall Handle_LoadGraphicsPipeline(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT __stdcall Handle_LoadComputePipeline(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT __stdcall Handle_LoadPipeline(ID3D12PipelineLibrary1*, LPCWSTR, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
	HRESULT __stdcall Handle_StorePipeline(ID3D12PipelineLibrary*, LPCWSTR, ID3D12PipelineState*);
	SIZE_T __stdcall Handle_GetSerializedSize(ID3D12PipelineLibrary*);
	HRESULT __stdcall Handle_Serialize(ID3D12PipelineLibrary*, void*, SIZE_T);
	void HookPipelineLibrary(ID3D12PipelineLibrary*);

	void STDMETHODCALLTYPE Handle_SetPipelineState(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	HRESULT STDMETHODCALLTYPE Handle_ResetGraphicsCommandList(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootSignature(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
	void STDMETHODCALLTYPE Handle_SetComputeRootSignature(ID3D12GraphicsCommandList*, ID3D12RootSignature*);

	HRESULT STDMETHODCALLTYPE Handle_PresentD3D12(IDXGISwapChain3*, UINT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_Present1D3D12(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityPresent(IDXGISwapChain3*, UINT, UINT);
	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityPresent1(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityResizeBuffers(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityPresent(IDXGISwapChain3*, UINT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityPresent1(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	void STDMETHODCALLTYPE Handle_ExecuteCommandListsD3D12(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
	HRESULT STDMETHODCALLTYPE Handle_ResizeBuffersD3D12(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityResizeBuffers(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

	HRESULT STDMETHODCALLTYPE Handle_CreateDescriptorHeap(ID3D12Device*, const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**);
	void STDMETHODCALLTYPE Handle_CreateConstantBufferView(ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateShaderResourceView(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateUnorderedAccessView(ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateRenderTargetView(ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateDepthStencilView(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateSampler(ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CopyDescriptors(ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	void STDMETHODCALLTYPE Handle_CopyDescriptorsSimple(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
	HRESULT STDMETHODCALLTYPE Handle_CreateCommittedResource(ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreatePlacedResource(ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreateReservedResource(ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);

	void STDMETHODCALLTYPE Handle_DrawInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_DrawIndexedInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	void STDMETHODCALLTYPE Handle_Dispatch(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_IASetPrimitiveTopology(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	void STDMETHODCALLTYPE Handle_RSSetViewports(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	void STDMETHODCALLTYPE Handle_RSSetScissorRects(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	void STDMETHODCALLTYPE Handle_SetDescriptorHeaps(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
	void STDMETHODCALLTYPE Handle_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Handle_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Handle_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_IASetIndexBuffer(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Handle_IASetVertexBuffers(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Handle_OMSetRenderTargets(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	void STDMETHODCALLTYPE Handle_ExecuteIndirect(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
}
