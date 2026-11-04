#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ShaderTarget/ShaderTarget.h"
#include "ModifiedShader/ModifiedShader.h"
#include "ShaderDiscovery/ShaderTargetProgress.h"

namespace HookD3D12
{
	struct GraphicsPipelineInfo;
	struct PipelineStateInfo;
} //namespace HookD3D12

namespace ShaderAutomaticDiscovery
{
	//Capture hooks enqueue unique shaders. The render path materializes a small,
	//bounded amount of work within a short cooperative budget; target preparation runs asynchronously.
	void ProcessQueuedWork(size_t maximumJobs = 1);
	ShaderTargetProgress GetProgress();
	void Shutdown();
	void RefreshModifiedShaderIndex(const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& modifiedShaders);
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
} //namespace ShaderAutomaticDiscovery
