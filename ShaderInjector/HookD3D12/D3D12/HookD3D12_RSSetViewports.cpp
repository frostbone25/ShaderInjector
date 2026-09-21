#include "../HookD3D12.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_RSSetViewports(ID3D12GraphicsCommandList* commandList, UINT viewportCount, const D3D12_VIEWPORT* viewports)
	{
		Handle_RSSetViewports(commandList, viewportCount, viewports);
	}

	void STDMETHODCALLTYPE Handle_RSSetViewports(ID3D12GraphicsCommandList* commandList, UINT viewportCount, const D3D12_VIEWPORT* viewports)
	{
		if (Globals::gShaderInjectorEnabled && RenderPassRuntime::IsGraphicsStateTrackingRequired() && !IsInsideRenderPassInjection())
			RenderPassRuntime::TrackViewports(commandList, viewportCount, viewports);

		Original_RSSetViewports(commandList, viewportCount, viewports);
	}
}
