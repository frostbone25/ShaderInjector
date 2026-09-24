#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/Runtime/DescriptorHeapState.h"
#include "RenderPass/Runtime/RootBindingState.h"

namespace RenderPassRuntime
{
	//mirror the game state a custom pass must restore after injecting commands.
	struct CommandListRenderState
	{
		ID3D12PipelineState* pipelineState = nullptr;
		ID3D12PipelineState* boundPipelineState = nullptr;
		ID3D12RootSignature* graphicsRootSignature = nullptr;
		ID3D12RootSignature* computeRootSignature = nullptr;
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		std::vector<DescriptorHeapState> descriptorHeaps;
		std::vector<RootBindingState> graphicsRootBindings;
		std::vector<RootBindingState> computeRootBindings;
		std::vector<RenderPass::ResourceBindingDiagnostic> inputBindings;
		std::vector<RenderPass::ResourceBindingDiagnostic> outputBindings;
		std::vector<D3D12_VIEWPORT> viewports;
		std::vector<D3D12_RECT> scissorRectangles;
		UINT descriptorIncrementSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES]{};
		bool descriptorIncrementSizesInitialized = false;
		uint32_t graphicsExecutionBoundaryMask = 0;
		uint32_t computeExecutionBoundaryMask = 0;
		uint64_t executionTrackingGeneration = 0;
		bool replacementPassActive = false;

		CommandListRenderState()
		{
			//reserve common D3D12 limits because these vectors update on hot hook paths.
			descriptorHeaps.reserve(2);
			graphicsRootBindings.reserve(24);
			computeRootBindings.reserve(24);
			outputBindings.reserve(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1);
			viewports.reserve(D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
			scissorRectangles.reserve(D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
		}
	};
} //namespace RenderPassRuntime
