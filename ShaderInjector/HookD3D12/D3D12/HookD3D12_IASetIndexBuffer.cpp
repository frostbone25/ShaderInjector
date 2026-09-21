#include "HookD3D12HookHandlers.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_IASetIndexBuffer(ID3D12GraphicsCommandList* commandList, const D3D12_INDEX_BUFFER_VIEW* view)
	{
		Handle_IASetIndexBuffer(commandList, view);
	}

	void STDMETHODCALLTYPE Handle_IASetIndexBuffer(ID3D12GraphicsCommandList* commandList, const D3D12_INDEX_BUFFER_VIEW* view)
	{
		if (Globals::gShaderInjectorEnabled &&
			RenderPassRuntime::IsResourceTrackingRequired() &&
			RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) &&
			!IsInsideRenderPassInjection())
			RenderPassRuntime::TrackIndexBuffer(commandList, view);

		Original_IASetIndexBuffer(commandList, view);
	}
}
