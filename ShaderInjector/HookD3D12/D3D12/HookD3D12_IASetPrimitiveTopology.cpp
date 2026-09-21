#include "../HookD3D12.h"

#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_IASetPrimitiveTopology(ID3D12GraphicsCommandList* commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		Handle_IASetPrimitiveTopology(commandList, topology);
	}

	void STDMETHODCALLTYPE Handle_IASetPrimitiveTopology(ID3D12GraphicsCommandList* commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (Globals::gShaderInjectorEnabled &&
			RenderPassRuntime::IsGraphicsStateTrackingRequired() &&
			!IsInsideRenderPassInjection())
			RenderPassRuntime::TrackPrimitiveTopology(commandList, topology);

		Original_IASetPrimitiveTopology(commandList, topology);
	}
}
