#include "../HookD3D12.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_IASetVertexBuffers(ID3D12GraphicsCommandList* commandList, UINT startSlot, UINT viewCount, const D3D12_VERTEX_BUFFER_VIEW* views)
	{
		Handle_IASetVertexBuffers(commandList, startSlot, viewCount, views);
	}

	void STDMETHODCALLTYPE Handle_IASetVertexBuffers(ID3D12GraphicsCommandList* commandList, UINT startSlot, UINT viewCount, const D3D12_VERTEX_BUFFER_VIEW* views)
	{
		if (Globals::gShaderInjectorEnabled &&
			RenderPassRuntime::IsResourceTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) &&
			!IsInsideRenderPassInjection())
			RenderPassRuntime::TrackVertexBuffers(commandList, startSlot, viewCount, views);

		Original_IASetVertexBuffers(commandList, startSlot, viewCount, views);
	}
}
