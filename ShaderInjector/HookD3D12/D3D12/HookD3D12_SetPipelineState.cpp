#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"
#include "../HookD3D12PipelineRegistry.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState)
	{
		Handle_SetPipelineState(commandList, pipelineState);
	}

	void STDMETHODCALLTYPE Handle_SetPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState)
	{
		if (IsInsideRenderPassInjection())
		{
			Original_SetPipelineState(commandList, pipelineState);
			return;
		}

		if (!Globals::gShaderInjectorEnabled)
		{
			Original_SetPipelineState(commandList, pipelineState);
			return;
		}

		PerformanceMetrics::ScopedTimer setPipelineStateTimer(PerformanceMetrics::Timing::SetPipelineStateHook, 256);
		ID3D12PipelineState* boundPipelineState = pipelineState;
		RenderPassRuntime::TrackPipelineState(commandList, pipelineState);
		CommandListPipelineState& commandListState = GetCommandListPipelineState(commandList);
		commandListState.pipelineState.store(pipelineState, std::memory_order_release);

		if (TryResolvePublishedPipelineState(pipelineState, boundPipelineState))
		{
			RenderPassRuntime::TrackBoundPipelineState(commandList, boundPipelineState);
			Original_SetPipelineState(commandList, boundPipelineState);
			return;
		}

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);

			if (gPipelineStateOverridesDirty.load(std::memory_order_acquire))
				RebuildPipelineStateOverrideMap();

			if (!IsKnownPipelineStateLocked(pipelineState))
			{
				ID3D12RootSignature* observedGraphicsRootSignature = commandListState.graphicsRootSignature.load(std::memory_order_acquire);
				ID3D12RootSignature* observedComputeRootSignature = commandListState.computeRootSignature.load(std::memory_order_acquire);
				const bool newlyObservedPipelineState = MarkUntrackedBoundPipelineStateLocked(pipelineState);
				bool needsRootSignatureRefresh = false;
				auto uncapturedIndexIt = gUncapturedPipelineStateIndexByPointer.find(pipelineState);

				if (uncapturedIndexIt != gUncapturedPipelineStateIndexByPointer.end() && uncapturedIndexIt->second < gUncapturedPipelineStates.size())
					needsRootSignatureRefresh = gUncapturedPipelineStates[uncapturedIndexIt->second].retryReplacementOnRootSignatureChange;

				if (newlyObservedPipelineState || needsRootSignatureRefresh)
				{
					RecordUncapturedPipelineStateLocked(
						pipelineState,
						observedGraphicsRootSignature,
						observedComputeRootSignature,
						"SetPipelineState bound");
				}
			}

			auto overrideIt = gPipelineStateOverrides.find(pipelineState);
			if (overrideIt != gPipelineStateOverrides.end() && overrideIt->second)
				boundPipelineState = overrideIt->second;
		}

		RenderPassRuntime::TrackBoundPipelineState(commandList, boundPipelineState);
		Original_SetPipelineState(commandList, boundPipelineState);
	}
}
