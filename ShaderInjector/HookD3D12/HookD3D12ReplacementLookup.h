#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>

//custom
#include "HookD3D12.h"
#include "ShaderTarget/ShaderTarget.h"

namespace HookD3D12
{
	bool GetPipelineCachedBlobInfo(ID3D12PipelineState* pipelineState, uint64_t& outHash, SIZE_T& outSize, std::vector<uint8_t>* outBytes = nullptr);
	bool SupportsCachedBlobContentMatching(SIZE_T cachedBlobSize);
	bool SupportsCachedBlobMetadataMatching(SIZE_T cachedBlobSize);
	void ResetCachedBlobContentLookup();
	bool MatchPersistedCachedBlobContent(
		const std::string& persistedBlobPath,
		const std::string& persistedBlobLength,
		const std::vector<uint8_t>& currentBlob,
		double& outMatchingRatio,
		size_t& outLongestMatchingRun);
	bool PersistedPipelineEntryTargetsShader(
		const ShaderTarget::ShaderTargetDisk& replacement,
		const ShaderTarget::ShaderTargetDisk& pipelineEntry);
	bool PersistedPipelineEntryTargetsShader(
		const ShaderTarget::ShaderTargetDisk& replacement,
		const ShaderTarget::ShaderPipelineTemplateDisk& pipelineEntry);
	bool PersistedPipelineStreamsAreEquivalent(
		const std::string& firstStreamPath,
		const std::string& secondStreamPath);
	bool ReplacementHasCachedBlobHash(const ShaderTarget::ShaderTargetDisk& replacement, uint64_t cachedBlobHash);
	int FindEnabledShaderTargetByCachedBlob(uint64_t cachedBlobHash);
	int FindEnabledShaderTargetByCachedBlobMetadata(
		SIZE_T cachedBlobSize,
		uint64_t rootSignatureHash,
		bool computePipeline);
	int FindEnabledShaderTargetByCachedBlobContent(
		const std::vector<uint8_t>& cachedBlob,
		double& outMatchingRatio,
		size_t& outLongestMatchingRun);
	bool ReplacementHashMatches(uint64_t pipelineHash, const std::string& replacementHash);
	bool GraphicsPipelineMatchesReplacementTemplate(const GraphicsPipelineInfo& pipeline, const ShaderTarget::ShaderTargetDisk& replacement);
	bool StreamPipelineMatchesReplacementTemplate(const PipelineStateInfo& pipeline, const ShaderTarget::ShaderTargetDisk& replacement);
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubobjectTypeForShaderType(ShaderTarget::ShaderType shaderType);
	const ShaderTarget::ShaderTargetDisk* FindActiveShaderTarget(
		const std::string& shaderTargetName,
		uint64_t shaderHash,
		ShaderTarget::ShaderType shaderType);
}
