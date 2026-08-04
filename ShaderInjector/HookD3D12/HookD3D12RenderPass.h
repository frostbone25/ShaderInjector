#pragma once

#include <d3d12.h>

namespace HookD3D12
{
	// Route injected API work through the live COM vtables so capture layers and
	// overlays can observe it without recursively invoking our own pass logic.
	class ScopedRenderPassInjection
	{
	public:
		ScopedRenderPassInjection();
		~ScopedRenderPassInjection();

		ScopedRenderPassInjection(const ScopedRenderPassInjection&) = delete;
		ScopedRenderPassInjection& operator=(const ScopedRenderPassInjection&) = delete;
	};

	bool IsInsideRenderPassInjection();

	using FunctionDrawInstancedD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	using FunctionDrawIndexedInstancedD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	using FunctionDispatchD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using FunctionIASetPrimitiveTopologyD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	using FunctionRSSetViewportsD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	using FunctionRSSetScissorRectsD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	using FunctionSetDescriptorHeapsD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
	using FunctionSetRootDescriptorTableD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	using FunctionSetRoot32BitConstantD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using FunctionSetRoot32BitConstantsD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	using FunctionSetRootDescriptorD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	using FunctionIASetIndexBufferD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	using FunctionIASetVertexBuffersD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	using FunctionOMSetRenderTargetsD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	using FunctionExecuteIndirectD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);

	extern FunctionDrawInstancedD3D12 Original_DrawInstanced;
	extern FunctionDrawIndexedInstancedD3D12 Original_DrawIndexedInstanced;
	extern FunctionDispatchD3D12 Original_Dispatch;
	extern FunctionIASetPrimitiveTopologyD3D12 Original_IASetPrimitiveTopology;
	extern FunctionRSSetViewportsD3D12 Original_RSSetViewports;
	extern FunctionRSSetScissorRectsD3D12 Original_RSSetScissorRects;
	extern FunctionSetDescriptorHeapsD3D12 Original_SetDescriptorHeaps;
	extern FunctionSetRootDescriptorTableD3D12 Original_SetComputeRootDescriptorTable;
	extern FunctionSetRootDescriptorTableD3D12 Original_SetGraphicsRootDescriptorTable;
	extern FunctionSetRoot32BitConstantD3D12 Original_SetComputeRoot32BitConstant;
	extern FunctionSetRoot32BitConstantD3D12 Original_SetGraphicsRoot32BitConstant;
	extern FunctionSetRoot32BitConstantsD3D12 Original_SetComputeRoot32BitConstants;
	extern FunctionSetRoot32BitConstantsD3D12 Original_SetGraphicsRoot32BitConstants;
	extern FunctionSetRootDescriptorD3D12 Original_SetComputeRootConstantBufferView;
	extern FunctionSetRootDescriptorD3D12 Original_SetGraphicsRootConstantBufferView;
	extern FunctionSetRootDescriptorD3D12 Original_SetComputeRootShaderResourceView;
	extern FunctionSetRootDescriptorD3D12 Original_SetGraphicsRootShaderResourceView;
	extern FunctionSetRootDescriptorD3D12 Original_SetComputeRootUnorderedAccessView;
	extern FunctionSetRootDescriptorD3D12 Original_SetGraphicsRootUnorderedAccessView;
	extern FunctionIASetIndexBufferD3D12 Original_IASetIndexBuffer;
	extern FunctionIASetVertexBuffersD3D12 Original_IASetVertexBuffers;
	extern FunctionOMSetRenderTargetsD3D12 Original_OMSetRenderTargets;
	extern FunctionExecuteIndirectD3D12 Original_ExecuteIndirect;

	void STDMETHODCALLTYPE Hook_DrawInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	void STDMETHODCALLTYPE Hook_Dispatch(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Hook_IASetPrimitiveTopology(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	void STDMETHODCALLTYPE Hook_RSSetViewports(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	void STDMETHODCALLTYPE Hook_RSSetScissorRects(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	void STDMETHODCALLTYPE Hook_SetDescriptorHeaps(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
	void STDMETHODCALLTYPE Hook_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Hook_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_IASetIndexBuffer(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Hook_IASetVertexBuffers(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Hook_OMSetRenderTargets(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	void STDMETHODCALLTYPE Hook_ExecuteIndirect(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
}
