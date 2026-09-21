#include "HookD3D12HookHandlers.h"

#include <atomic>

#include "HookD3D12ExecutionHookHelpers.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	std::atomic<bool> gLoggedDrawInstancedHook = false;

	void STDMETHODCALLTYPE Hook_DrawInstanced(ID3D12GraphicsCommandList* commandList, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
	{
		Handle_DrawInstanced(commandList, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	}

	void STDMETHODCALLTYPE Handle_DrawInstanced(ID3D12GraphicsCommandList* commandList, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
	{
		if (!Globals::gShaderInjectorEnabled || !RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) || IsInsideRenderPassInjection())
		{
			Original_DrawInstanced(commandList, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
			return;
		}

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::DrawInstanced);
		thread_local bool commandHookLogChecked = false;

		if (!commandHookLogChecked)
		{
			LogFirstCommandHookHit(gLoggedDrawInstancedHook, "Hook_DrawInstanced", commandList);
			commandHookLogChecked = true;
		}

		uint32_t boundaryMask = 0;
		{
			PerformanceMetrics::ScopedTimer lookupTimer(PerformanceMetrics::Timing::ExecutionHookLookup, 512);
			boundaryMask = RenderPassRuntime::GetExecutionBoundaryMask(commandList, false);
		}

		if (boundaryMask)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::ExecutionBoundaryCandidate);

		if ((boundaryMask & 1u) != 0)
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::Before, "DrawInstanced");

		Original_DrawInstanced(commandList, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);

		if ((boundaryMask & 1u) != 0)
			RenderPassRuntime::CompleteGraphicsExecutionBoundary(commandList);

		if ((boundaryMask & 2u) != 0)
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::After, "DrawInstanced");
	}
}
