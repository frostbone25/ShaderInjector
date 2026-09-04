#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"
#include "../HookD3D12PipelineRegistry.h"
#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	HRESULT STDMETHODCALLTYPE Hook_ResetGraphicsCommandList(ID3D12GraphicsCommandList* commandList, ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState)
	{
		return Handle_ResetGraphicsCommandList(commandList, allocator, initialState);
	}

	HRESULT STDMETHODCALLTYPE Handle_ResetGraphicsCommandList(ID3D12GraphicsCommandList* commandList, ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState)
	{
		const bool trackRenderPassState = Globals::gShaderInjectorEnabled && RenderPassRuntime::IsTrackingRequired();
		const bool retireRecordedRenderPassWork = RenderPassRuntime::HasPendingCommandListSubmissionWork();

		if (trackRenderPassState)
			RenderPassRuntime::ResetCommandList(commandList, initialState);

		const auto resetCommandList = [&](ID3D12PipelineState* pipelineState)
		{
			const HRESULT result = Original_ResetGraphicsCommandList(commandList, allocator, pipelineState);
			if (trackRenderPassState || retireRecordedRenderPassWork)
				RenderPassRuntime::CompleteCommandListReset(commandList, SUCCEEDED(result));
			return result;
		};

		CommandListPipelineState& commandListState = GetCommandListPipelineState(commandList);
		commandListState.graphicsRootSignature.store(nullptr, std::memory_order_release);
		commandListState.computeRootSignature.store(nullptr, std::memory_order_release);

		if (!Globals::gShaderInjectorEnabled)
		{
			commandListState.pipelineState.store(nullptr, std::memory_order_release);
			return resetCommandList(initialState);
		}

		ID3D12PipelineState* boundState = initialState;
		commandListState.pipelineState.store(initialState, std::memory_order_release);

		if (TryResolvePublishedPipelineState(initialState, boundState))
		{
			RenderPassRuntime::TrackBoundPipelineState(commandList, boundState);
			return resetCommandList(boundState);
		}

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);

			if (gPipelineStateOverridesDirty.load(std::memory_order_acquire))
				RebuildPipelineStateOverrideMap();

			if (!IsKnownPipelineStateLocked(initialState))
			{
				const bool newlyObservedPipelineState = MarkUntrackedBoundPipelineStateLocked(initialState);
				bool needsRootSignatureRefresh = false;
				auto uncapturedIndexIt = gUncapturedPipelineStateIndexByPointer.find(initialState);

				if (uncapturedIndexIt != gUncapturedPipelineStateIndexByPointer.end() && uncapturedIndexIt->second < gUncapturedPipelineStates.size())
					needsRootSignatureRefresh = gUncapturedPipelineStates[uncapturedIndexIt->second].retryReplacementOnRootSignatureChange;

				if (newlyObservedPipelineState || needsRootSignatureRefresh)
					RecordUncapturedPipelineStateLocked(initialState, nullptr, nullptr, "CommandList Reset bound initial");
			}

			auto overrideIt = gPipelineStateOverrides.find(initialState);
			if (overrideIt != gPipelineStateOverrides.end() && overrideIt->second)
				boundState = overrideIt->second;
		}

		RenderPassRuntime::TrackBoundPipelineState(commandList, boundState);
		return resetCommandList(boundState);
	}
}
