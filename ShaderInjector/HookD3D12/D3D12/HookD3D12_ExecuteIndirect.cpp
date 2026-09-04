#include "HookD3D12HookHandlers.h"

#include <atomic>

#include "HookD3D12ExecutionHookHelpers.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	namespace
	{
		std::atomic<bool> gLoggedExecuteIndirectHook = false;
	}

	void STDMETHODCALLTYPE Hook_ExecuteIndirect(ID3D12GraphicsCommandList* commandList, ID3D12CommandSignature* commandSignature, UINT maximumCommandCount, ID3D12Resource* argumentBuffer, UINT64 argumentBufferOffset, ID3D12Resource* countBuffer, UINT64 countBufferOffset)
	{
		Handle_ExecuteIndirect(commandList, commandSignature, maximumCommandCount, argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
	}

	void STDMETHODCALLTYPE Handle_ExecuteIndirect(ID3D12GraphicsCommandList* commandList, ID3D12CommandSignature* commandSignature, UINT maximumCommandCount, ID3D12Resource* argumentBuffer, UINT64 argumentBufferOffset, ID3D12Resource* countBuffer, UINT64 countBufferOffset)
	{
		if (!Globals::gShaderInjectorEnabled || !RenderPassRuntime::IsPipelineExecutionTrackingRequired(false) || IsInsideRenderPassInjection())
		{
			Original_ExecuteIndirect(commandList, commandSignature, maximumCommandCount, argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
			return;
		}

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ExecuteIndirect);
		thread_local bool commandHookLogChecked = false;
		if (!commandHookLogChecked)
		{
			LogFirstCommandHookHit(gLoggedExecuteIndirectHook, "Hook_ExecuteIndirect", commandList);
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
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::Before, "ExecuteIndirect");

		Original_ExecuteIndirect(commandList, commandSignature, maximumCommandCount, argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
		if ((boundaryMask & 1u) != 0)
			RenderPassRuntime::CompleteGraphicsExecutionBoundary(commandList);
		if ((boundaryMask & 2u) != 0)
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::After, "ExecuteIndirect");
	}
}
