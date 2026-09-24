#pragma once
#include "Enum/RenderPassMipChainRootArgumentType.h"

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass.h"
#include "RenderPassTexturePool.h"
#include "RenderPass/MipChain/DescriptorHeapBinding.h"
#include "RenderPass/MipChain/RootArgumentSnapshot.h"
#include "RenderPass/MipChain/GraphicsStateSnapshot.h"
#include "RenderPass/MipChain/ExecutionResult.h"

namespace RenderPassMipChain
{
	bool GenerateRuntimeMipChain(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const RenderPassTexturePool::TextureView& source,
		const GraphicsStateSnapshot& gameState,
		const GraphicsStateSnapshot& oppositePipelineState,
		bool computePipeline,
		RenderPassTexturePool::TextureView& outTexture,
		std::string& outError);

	void PrepareForTargetDraw(
		const std::vector<const RenderPass::RenderPassDisk*>& renderPasses,
		ID3D12GraphicsCommandList* commandList,
		const GraphicsStateSnapshot& gameState,
		bool computePipeline,
		std::vector<ExecutionResult>& outResults);
	void RestoreAfterTargetDraw(
		ID3D12GraphicsCommandList* commandList,
		const GraphicsStateSnapshot& gameState,
		bool computePipeline);
	bool HasRecordedCommandListWork();
	void ResetCommandListRecording(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists);
	void ReleaseResources();
} //namespace RenderPassMipChain
