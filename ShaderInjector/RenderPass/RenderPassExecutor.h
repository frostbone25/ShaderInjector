#pragma once

#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass.h"
#include "RenderPass/RenderPassMipChain.h"
#include "RenderPass/RenderPassTexturePool.h"

namespace RenderPassExecutor
{
	bool ExecuteFullscreenTriangle(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		ID3D12RootSignature* graphicsRootSignature,
		ID3D12PipelineState* pipelineStateToRestore,
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopologyToRestore,
		const std::vector<RenderPass::ResourceBindingDiagnostic>& outputBindings,
		const RenderPassTexturePool::TextureView* runtimeOutput,
		const RenderPassMipChain::GraphicsStateSnapshot* gameStateToRestore,
		std::string& outError);
	bool ExecuteCompute(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		ID3D12PipelineState* computePipelineState,
		ID3D12PipelineState* pipelineStateToRestore,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		const std::vector<RenderPassTexturePool::TextureView>& unorderedAccessOutputs,
		std::string& outError);
	bool ExecuteTextureCopy(
		ID3D12GraphicsCommandList* commandList,
		const RenderPassTexturePool::TextureView& source,
		const RenderPassTexturePool::TextureView& destination,
		std::string& outError);
	void ReleaseResources();
}
