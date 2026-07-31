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
	bool GetPipelineCachedBlobInfo(ID3D12PipelineState* pipelineState, uint64_t& outHash, SIZE_T& outSize, std::vector<uint8_t>* outBytes = nullptr);
	bool SupportsCachedBlobContentMatching(SIZE_T cachedBlobSize);
	void ResetCachedBlobContentLookup();
	bool ReplacementHasCachedBlobHash(const ShaderTarget::ShaderTargetDisk& replacement, uint64_t cachedBlobHash);
	int FindEnabledShaderTargetByCachedBlob(uint64_t cachedBlobHash);
	int FindEnabledShaderTargetByCachedBlobContent(
		const std::vector<uint8_t>& cachedBlob,
		double& outMatchingRatio,
		size_t& outLongestMatchingRun);
	bool ReplacementHashMatches(uint64_t pipelineHash, const std::string& replacementHash);
	bool GraphicsPipelineMatchesReplacementTemplate(const GraphicsPipelineInfo& pipeline, const ShaderTarget::ShaderTargetDisk& replacement);
	bool StreamPipelineMatchesReplacementTemplate(const PipelineStateInfo& pipeline, const ShaderTarget::ShaderTargetDisk& replacement);
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubobjectTypeForShaderType(ShaderTarget::ShaderType shaderType);
	bool ReplacementStillEnabled(const std::string& replacementName, uint64_t shaderHash, ShaderTarget::ShaderType shaderType);
}
