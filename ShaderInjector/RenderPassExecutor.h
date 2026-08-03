#pragma once

#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass.h"

namespace RenderPassExecutor
{
	bool ExecuteFullscreenTriangle(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		ID3D12RootSignature* graphicsRootSignature,
		ID3D12PipelineState* pipelineStateToRestore,
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopologyToRestore,
		const std::vector<RenderPass::ResourceBindingDiagnostic>& outputBindings,
		std::string& outError);
	void ReleaseResources();
}
