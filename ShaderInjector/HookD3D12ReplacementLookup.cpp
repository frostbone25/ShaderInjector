#include "HookD3D12ReplacementLookup.h"

#include "Hash.h"

namespace HookD3D12
{
	bool GetPipelineCachedBlobInfo(ID3D12PipelineState* pipelineState, uint64_t& outHash, SIZE_T& outSize, std::vector<uint8_t>* outBytes)
	{
		outHash = 0;
		outSize = 0;

		if (outBytes)
			outBytes->clear();

		if (!pipelineState)
			return false;

		ID3DBlob* cachedBlob = nullptr;
		HRESULT hr = pipelineState->GetCachedBlob(&cachedBlob);

		if (FAILED(hr) || !cachedBlob)
			return false;

		const void* blobData = cachedBlob->GetBufferPointer();
		const SIZE_T blobSize = cachedBlob->GetBufferSize();

		if (blobData && blobSize > 0)
		{
			outHash = Hash::HashMemory(blobData, blobSize);
			outSize = blobSize;

			if (outBytes)
			{
				const uint8_t* bytes = static_cast<const uint8_t*>(blobData);
				outBytes->assign(bytes, bytes + blobSize);
			}
		}

		cachedBlob->Release();
		return outHash != 0;
	}

	bool ReplacementHasCachedBlobSize(const ShaderReplacement::ShaderReplacementDisk& replacement, SIZE_T cachedBlobSize)
	{
		if (!cachedBlobSize)
			return false;

		if (!replacement.pipelineCachedBlobLength.empty())
		{
			const SIZE_T replacementSize = (SIZE_T)_strtoui64(replacement.pipelineCachedBlobLength.c_str(), nullptr, 10);
			if (replacementSize == cachedBlobSize)
				return true;
		}

		for (const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			if (pipelineTemplate.pipelineCachedBlobLength.empty())
				continue;

			const SIZE_T templateSize = (SIZE_T)_strtoui64(pipelineTemplate.pipelineCachedBlobLength.c_str(), nullptr, 10);
			if (templateSize == cachedBlobSize)
				return true;
		}

		return false;
	}

	bool ReplacementHasCachedBlobHash(const ShaderReplacement::ShaderReplacementDisk& replacement, uint64_t cachedBlobHash)
	{
		if (!cachedBlobHash)
			return false;

		if (Hash::ParseHashText(replacement.pipelineCachedBlobHash) == cachedBlobHash)
			return true;

		for (const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			if (Hash::ParseHashText(pipelineTemplate.pipelineCachedBlobHash) == cachedBlobHash)
				return true;
		}

		return false;
	}

	int CountEnabledShaderReplacementsByCachedBlobSize(SIZE_T cachedBlobSize)
	{
		if (!cachedBlobSize)
			return 0;

		int matchCount = 0;
		for (const auto& replacement : gLoadedShaderReplacements)
		{
			if (!replacement.enabled)
				continue;

			if (ReplacementHasCachedBlobSize(replacement, cachedBlobSize))
				matchCount++;
		}

		return matchCount;
	}

	int FindUniqueEnabledShaderReplacementByCachedBlobSize(SIZE_T cachedBlobSize)
	{
		if (!cachedBlobSize)
			return -1;

		int matchIndex = -1;
		int matchCount = 0;

		for (int i = 0; i < (int)gLoadedShaderReplacements.size(); i++)
		{
			const ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[i];

			if (!replacement.enabled)
				continue;

			if (ReplacementHasCachedBlobSize(replacement, cachedBlobSize))
			{
				matchIndex = i;
				matchCount++;
			}
		}

		return matchCount == 1 ? matchIndex : -1;
	}

	int FindEnabledShaderReplacementByCachedBlob(uint64_t cachedBlobHash)
	{
		if (!cachedBlobHash)
			return -1;

		for (int i = 0; i < (int)gLoadedShaderReplacements.size(); i++)
		{
			const ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[i];

			if (!replacement.enabled)
				continue;

			if (ReplacementHasCachedBlobHash(replacement, cachedBlobHash))
				return i;
		}

		return -1;
	}

	bool ReplacementHashMatches(uint64_t pipelineHash, const std::string& replacementHash)
	{
		const uint64_t parsedHash = Hash::ParseHashText(replacementHash);
		return parsedHash == 0 || parsedHash == pipelineHash;
	}

	bool GraphicsPipelineMatchesReplacementTemplate(const GraphicsPipelineInfo& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		return ReplacementHashMatches(pipeline.vsHash, replacement.vsHash) &&
			ReplacementHashMatches(pipeline.psHash, replacement.psHash) &&
			ReplacementHashMatches(pipeline.gsHash, replacement.gsHash) &&
			ReplacementHashMatches(pipeline.hsHash, replacement.hsHash) &&
			ReplacementHashMatches(pipeline.dsHash, replacement.dsHash);
	}

	bool StreamPipelineMatchesReplacementTemplate(const PipelineStateInfo& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		return ReplacementHashMatches(pipeline.vsHash, replacement.vsHash) &&
			ReplacementHashMatches(pipeline.psHash, replacement.psHash) &&
			ReplacementHashMatches(pipeline.csHash, replacement.csHash) &&
			ReplacementHashMatches(pipeline.gsHash, replacement.gsHash) &&
			ReplacementHashMatches(pipeline.hsHash, replacement.hsHash) &&
			ReplacementHashMatches(pipeline.dsHash, replacement.dsHash);
	}

	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubobjectTypeForShaderType(ShaderReplacement::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderReplacement::VertexShader:   return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
			case ShaderReplacement::PixelShader:    return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
			case ShaderReplacement::GeometryShader: return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS;
			case ShaderReplacement::HullShader:     return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS;
			case ShaderReplacement::DomainShader:   return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS;
			case ShaderReplacement::ComputeShader:  return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS;
			default: return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID;
		}
	}

	int FindEnabledShaderReplacement(uint64_t shaderHash, ShaderReplacement::ShaderType shaderType)
	{
		if (!shaderHash)
			return -1;

		for (int i = 0; i < (int)gLoadedShaderReplacements.size(); i++)
		{
			const ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[i];

			if (!replacement.enabled || replacement.shaderType != shaderType)
				continue;

			if (Hash::ParseHashText(replacement.originalShaderBytecodeHash) == shaderHash)
				return i;
		}

		return -1;
	}

	bool ReplacementStillEnabled(const std::string& replacementName, uint64_t shaderHash, ShaderReplacement::ShaderType shaderType)
	{
		const int replacementIndex = FindEnabledShaderReplacement(shaderHash, shaderType);

		if (replacementIndex < 0)
			return false;

		return gLoadedShaderReplacements[replacementIndex].name == replacementName;
	}
}
