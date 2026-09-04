#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetComputeRootConstantBufferView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetGraphicsRootConstantBufferView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetComputeRootShaderResourceView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetGraphicsRootShaderResourceView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetComputeRootUnorderedAccessView(commandList, rootParameterIndex, address);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		Handle_SetGraphicsRootUnorderedAccessView(commandList, rootParameterIndex, address);
	}

#define DEFINE_ROOT_DESCRIPTOR_HANDLER(HandlerName, OriginalName, IsCompute, BindingName) \
	void STDMETHODCALLTYPE HandlerName(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address) \
	{ \
		if (Globals::gShaderInjectorEnabled && \
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(IsCompute) && \
			RenderPassRuntime::IsRootBindingTrackingRequired() && \
			!IsInsideRenderPassInjection()) \
		{ \
			RenderPassRuntime::TrackRootDescriptor(commandList, IsCompute, BindingName, rootParameterIndex, address); \
		} \
		OriginalName(commandList, rootParameterIndex, address); \
	}

	DEFINE_ROOT_DESCRIPTOR_HANDLER(Handle_SetComputeRootConstantBufferView, Original_SetComputeRootConstantBufferView, true, "CBV")
	DEFINE_ROOT_DESCRIPTOR_HANDLER(Handle_SetGraphicsRootConstantBufferView, Original_SetGraphicsRootConstantBufferView, false, "CBV")
	DEFINE_ROOT_DESCRIPTOR_HANDLER(Handle_SetComputeRootShaderResourceView, Original_SetComputeRootShaderResourceView, true, "SRV")
	DEFINE_ROOT_DESCRIPTOR_HANDLER(Handle_SetGraphicsRootShaderResourceView, Original_SetGraphicsRootShaderResourceView, false, "SRV")
	DEFINE_ROOT_DESCRIPTOR_HANDLER(Handle_SetComputeRootUnorderedAccessView, Original_SetComputeRootUnorderedAccessView, true, "UAV")
	DEFINE_ROOT_DESCRIPTOR_HANDLER(Handle_SetGraphicsRootUnorderedAccessView, Original_SetGraphicsRootUnorderedAccessView, false, "UAV")

#undef DEFINE_ROOT_DESCRIPTOR_HANDLER
}
