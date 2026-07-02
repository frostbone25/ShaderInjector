#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ShaderTarget.h"
#include "ModifiedShader.h"

namespace HookD3D12
{
	struct GraphicsPipelineInfo;
	struct PipelineStateInfo;
}

namespace ShaderAutomaticDiscovery
{
	// Capture hooks only enqueue unique shaders. The render path drains a small,
	// bounded amount of work so shader analysis cannot stall PSO creation threads.
	void ProcessQueuedWork(size_t maximumJobs = 1);
	void RefreshModifiedShaderIndex(const std::vector<ModifiedShader::PackageDisk>& modifiedShaders);
	void ProcessCapturedGraphicsPipeline(const HookD3D12::GraphicsPipelineInfo& pipeline);
	void ProcessCapturedStreamPipeline(const HookD3D12::PipelineStateInfo& pipeline);

	bool ProcessCapturedShader(
		const HookD3D12::GraphicsPipelineInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const std::vector<uint8_t>& shaderBytecode);
	bool ProcessCapturedShader(
		const HookD3D12::PipelineStateInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const std::vector<uint8_t>& shaderBytecode);
}
