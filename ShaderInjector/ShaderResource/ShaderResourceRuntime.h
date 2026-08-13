#pragma once

#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassMipChain.h"

namespace ShaderResourceRuntime
{
	bool BindResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const RenderPassMipChain::GraphicsStateSnapshot& gameState,
		bool computePipeline,
		std::string& outError);
	void RestoreResources(ID3D12GraphicsCommandList* commandList);
	void ResetCommandList(ID3D12GraphicsCommandList* commandList);
	void ReleaseResources();
}
