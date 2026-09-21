#include "../HookD3D12.h"

#include <atomic>

#include "HookD3D12ExecutionHookHelpers.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	std::atomic<bool> gLoggedDrawIndexedInstancedHook = false;

	void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(ID3D12GraphicsCommandList* commandList, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
	{
		Handle_DrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
	}

	void STDMETHODCALLTYPE Handle_DrawIndexedInstanced(ID3D12GraphicsCommandList* commandList, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
	{
		if (!Globals::gShaderInjectorEnabled || !RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) || IsInsideRenderPassInjection())
		{
			Original_DrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
			return;
		}

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::DrawIndexedInstanced);
		thread_local bool commandHookLogChecked = false;

		if (!commandHookLogChecked)
		{
			LogFirstCommandHookHit(gLoggedDrawIndexedInstancedHook, "Hook_DrawIndexedInstanced", commandList);
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
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::Before, "DrawIndexedInstanced");

		Original_DrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);

		if ((boundaryMask & 1u) != 0)
			RenderPassRuntime::CompleteGraphicsExecutionBoundary(commandList);

		if ((boundaryMask & 2u) != 0)
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::After, "DrawIndexedInstanced");
	}
}
