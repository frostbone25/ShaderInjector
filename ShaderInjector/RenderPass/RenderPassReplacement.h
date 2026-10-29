#pragma once

#include <array>
#include <string>
#include <vector>

#include <d3d12.h>

#include "Enum/ShaderType.h"
#include "RenderPass/RenderPass.h"
#include "RenderPass/Runtime/ShaderTargetBinding.h"

namespace RenderPassReplacement
{
	//replacement bytecode per shader stage, indexed by ShaderTarget::ShaderType; null keeps the captured stage.
	struct StageOverrides
	{
		std::array<const std::vector<uint8_t>*, ShaderTarget::Unknown> stages{};
	};

	ID3D12PipelineState* GetOrCreatePipeline(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12PipelineState* originalPipelineState,
		const RenderPassRuntime::ShaderTargetBinding& shaderTarget,
		std::string& outError);
	//rebuild a captured PSO with replaced stages, optionally against another root signature.
	//the caller owns the returned reference.
	ID3D12PipelineState* BuildPipeline(
		ID3D12PipelineState* original,
		const StageOverrides& overrides,
		ID3D12RootSignature* rootSignature,
		bool computePipeline,
		std::string& outError);
	void InvalidateFailedPipelines();
	void ReleaseResources();
} //namespace RenderPassReplacement
