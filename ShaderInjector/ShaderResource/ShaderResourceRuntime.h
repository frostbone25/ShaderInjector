#pragma once

#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassMipChain.h"

namespace ShaderResourceRuntime
{
	struct RuntimeTextureBindingOverride
	{
		std::string resourceId;
		D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceView{};
		std::string unorderedAccessResourceId;
		D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessView{};
	};

	bool BindResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const RenderPassMipChain::GraphicsStateSnapshot& gameState,
		const RenderPassMipChain::GraphicsStateSnapshot& oppositePipelineState,
		bool computePipeline,
		std::string& outError,
		const RuntimeTextureBindingOverride* runtimeTextureOverride = nullptr);
	void RestoreResources(ID3D12GraphicsCommandList* commandList);
	bool HasRecordedCommandListWork();
	void ResetCommandList(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists);
	void ReleaseResources();
}
