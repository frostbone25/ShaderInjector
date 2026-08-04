#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>

//custom
#include "HookD3D12.h"
#include "ShaderTarget.h"

namespace HookD3D12
{
	bool LoadPersistedShaderBlob(const std::string& path, std::vector<uint8_t>& bytecode, uint64_t& hash, SIZE_T& size);
	bool LoadPersistedStreamTemplateFromReplacement(const ShaderTarget::ShaderTargetDisk& replacement, PipelineStateInfo& outPipeline);
	void BackfillReplacementPortableMetadataFromSidecars(ShaderTarget::ShaderTargetDisk& replacement);
	uint64_t StreamShaderHashForType(const PipelineStateInfo& pipeline, ShaderTarget::ShaderType shaderType);
	bool StreamPipelineHasShaderHash(const PipelineStateInfo& pipeline, ShaderTarget::ShaderType shaderType, uint64_t shaderHash);
	void FillPipelineTemplateCommonState(ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate, const PipelineStateInfo& pipeline);
	ShaderTarget::ShaderTargetDisk ReplacementWithPipelineTemplate(const ShaderTarget::ShaderTargetDisk& replacement, const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate);
	SIZE_T CountMatchingBytes(const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs);
	bool WriteStreamPipelineTemplateVariant(ShaderTarget::ShaderTargetDisk& replacement, const PipelineStateInfo& pipeline, int pipelineIndex, int templateIndex, bool& ok);
	void WriteMatchingStreamPipelineTemplateVariants(ShaderTarget::ShaderTargetDisk& replacement, ShaderTarget::ShaderType shaderType, uint64_t shaderHash, bool& ok);
	bool PersistAppliedStreamPipelineTemplate(
		ShaderTarget::ShaderTargetDisk& replacement,
		const PipelineStateInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash);
	bool PersistStreamPipelineTemplatesForShaderAlias(
		ShaderTarget::ShaderTargetDisk& replacement,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash);
	bool SelectPersistedPipelineTemplateForUncaptured(
		const ShaderTarget::ShaderTargetDisk& replacement,
		const UncapturedPipelineStateInfo& uncaptured,
		ShaderTarget::ShaderTargetDisk& outTemplateReplacement,
		std::string& outTemplateName,
		SIZE_T& outMatchingBytes);
}
