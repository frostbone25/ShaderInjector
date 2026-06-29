#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>

//custom
#include "HookD3D12.h"
#include "ShaderReplacement.h"

namespace HookD3D12
{
	bool GetPipelineCachedBlobInfo(ID3D12PipelineState* pipelineState, uint64_t& outHash, SIZE_T& outSize, std::vector<uint8_t>* outBytes = nullptr);
	void ResetCachedBlobContentLookup();
	bool ReplacementHasCachedBlobHash(const ShaderReplacement::ShaderReplacementDisk& replacement, uint64_t cachedBlobHash);
	int FindEnabledShaderReplacementByCachedBlob(uint64_t cachedBlobHash);
	int FindEnabledShaderReplacementByCachedBlobContent(
		const std::vector<uint8_t>& cachedBlob,
		double& outMatchingRatio,
		size_t& outLongestMatchingRun);
	bool ReplacementHashMatches(uint64_t pipelineHash, const std::string& replacementHash);
	bool GraphicsPipelineMatchesReplacementTemplate(const GraphicsPipelineInfo& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement);
	bool StreamPipelineMatchesReplacementTemplate(const PipelineStateInfo& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement);
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubobjectTypeForShaderType(ShaderReplacement::ShaderType shaderType);
	bool ReplacementStillEnabled(const std::string& replacementName, uint64_t shaderHash, ShaderReplacement::ShaderType shaderType);
}
