#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"
#include "../HookD3D12PipelineRegistry.h"
#include "Globals.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_SetGraphicsRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* rootSignature)
	{
		Handle_SetGraphicsRootSignature(commandList, rootSignature);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* rootSignature)
	{
		Handle_SetComputeRootSignature(commandList, rootSignature);
	}

	void STDMETHODCALLTYPE Handle_SetGraphicsRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* rootSignature)
	{
		if (!Globals::gShaderInjectorEnabled)
		{
			GetCommandListPipelineState(commandList).graphicsRootSignature.store(rootSignature, std::memory_order_release);
			Original_SetGraphicsRootSignature(commandList, rootSignature);
			return;
		}

		if (IsInsideRenderPassInjection())
		{
			Original_SetGraphicsRootSignature(commandList, rootSignature);
			return;
		}

		if (RenderPassRuntime::IsPipelineExecutionTrackingRequired(false))
			RenderPassRuntime::TrackRootSignature(commandList, false, rootSignature);

		CommandListPipelineState& commandListState = GetCommandListPipelineState(commandList);
		commandListState.graphicsRootSignature.store(rootSignature, std::memory_order_release);
		ID3D12PipelineState* currentPipelineState = commandListState.pipelineState.load(std::memory_order_acquire);

		if (currentPipelineState && !IsKnownPipelineStateLocked(currentPipelineState))
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			UpdateUncapturedPipelineRootSignatureLocked(currentPipelineState, rootSignature, false);
		}

		Original_SetGraphicsRootSignature(commandList, rootSignature);
	}

	void STDMETHODCALLTYPE Handle_SetComputeRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* rootSignature)
	{
		if (!Globals::gShaderInjectorEnabled)
		{
			GetCommandListPipelineState(commandList).computeRootSignature.store(rootSignature, std::memory_order_release);
			Original_SetComputeRootSignature(commandList, rootSignature);
			return;
		}

		if (IsInsideRenderPassInjection())
		{
			Original_SetComputeRootSignature(commandList, rootSignature);
			return;
		}

		if (RenderPassRuntime::IsPipelineExecutionTrackingRequired(true))
			RenderPassRuntime::TrackRootSignature(commandList, true, rootSignature);

		CommandListPipelineState& commandListState = GetCommandListPipelineState(commandList);
		commandListState.computeRootSignature.store(rootSignature, std::memory_order_release);
		ID3D12PipelineState* currentPipelineState = commandListState.pipelineState.load(std::memory_order_acquire);

		if (currentPipelineState && !IsKnownPipelineStateLocked(currentPipelineState))
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			UpdateUncapturedPipelineRootSignatureLocked(currentPipelineState, rootSignature, true);
		}

		Original_SetComputeRootSignature(commandList, rootSignature);
	}
}
