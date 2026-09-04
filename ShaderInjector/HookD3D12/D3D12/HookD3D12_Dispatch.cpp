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
		std::atomic<bool> gLoggedDispatchHook = false;
	}

	void STDMETHODCALLTYPE Hook_Dispatch(ID3D12GraphicsCommandList* commandList, UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ)
	{
		Handle_Dispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}

	void STDMETHODCALLTYPE Handle_Dispatch(ID3D12GraphicsCommandList* commandList, UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ)
	{
		if (!Globals::gShaderInjectorEnabled || !RenderPassRuntime::IsPipelineExecutionTrackingRequired(true) || IsInsideRenderPassInjection())
		{
			Original_Dispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::Dispatch);
		thread_local bool commandHookLogChecked = false;
		if (!commandHookLogChecked)
		{
			LogFirstCommandHookHit(gLoggedDispatchHook, "Hook_Dispatch", commandList);
			commandHookLogChecked = true;
		}

		uint32_t boundaryMask = 0;
		{
			PerformanceMetrics::ScopedTimer lookupTimer(PerformanceMetrics::Timing::ExecutionHookLookup, 512);
			boundaryMask = RenderPassRuntime::GetExecutionBoundaryMask(commandList, true);
		}
		if (boundaryMask)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::ExecutionBoundaryCandidate);
		if ((boundaryMask & 1u) != 0)
		{
			RenderPassRuntime::RecordExecutionBoundary(commandList, true, RenderPassRuntime::ExecutionBoundary::Before, "Dispatch", threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}

		Original_Dispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		if ((boundaryMask & 1u) != 0)
			RenderPassRuntime::CompleteComputeExecutionBoundary(commandList);
		if ((boundaryMask & 2u) != 0)
		{
			RenderPassRuntime::RecordExecutionBoundary(commandList, true, RenderPassRuntime::ExecutionBoundary::After, "Dispatch", threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}
	}
}
