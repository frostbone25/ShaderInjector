#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_IASetPrimitiveTopology(ID3D12GraphicsCommandList* commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		Handle_IASetPrimitiveTopology(commandList, topology);
	}

	void STDMETHODCALLTYPE Hook_IASetIndexBuffer(ID3D12GraphicsCommandList* commandList, const D3D12_INDEX_BUFFER_VIEW* view)
	{
		Handle_IASetIndexBuffer(commandList, view);
	}

	void STDMETHODCALLTYPE Hook_IASetVertexBuffers(ID3D12GraphicsCommandList* commandList, UINT startSlot, UINT viewCount, const D3D12_VERTEX_BUFFER_VIEW* views)
	{
		Handle_IASetVertexBuffers(commandList, startSlot, viewCount, views);
	}

	void STDMETHODCALLTYPE Handle_IASetPrimitiveTopology(ID3D12GraphicsCommandList* commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsGraphicsStateTrackingRequired() && !IsInsideRenderPassInjection())
			RenderPassRuntime::TrackPrimitiveTopology(commandList, topology);
		Original_IASetPrimitiveTopology(commandList, topology);
	}

	void STDMETHODCALLTYPE Handle_IASetIndexBuffer(ID3D12GraphicsCommandList* commandList, const D3D12_INDEX_BUFFER_VIEW* view)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsResourceTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackIndexBuffer(commandList, view);
		}
		Original_IASetIndexBuffer(commandList, view);
	}

	void STDMETHODCALLTYPE Handle_IASetVertexBuffers(ID3D12GraphicsCommandList* commandList, UINT startSlot, UINT viewCount, const D3D12_VERTEX_BUFFER_VIEW* views)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsResourceTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) && !IsInsideRenderPassInjection())
		{
			RenderPassRuntime::TrackVertexBuffers(commandList, startSlot, viewCount, views);
		}
		Original_IASetVertexBuffers(commandList, startSlot, viewCount, views);
	}
}
