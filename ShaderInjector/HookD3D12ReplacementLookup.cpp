//HookD3D12ReplacementLookup.cpp
#include "HookD3D12ReplacementLookup.h"

#include <algorithm>
#include <unordered_map>

//custom
#include "Hash.h"
#include "ShaderInjectorIO.h"

namespace HookD3D12
{
	namespace
	{
		// Tiny driver cache records can be mostly common bookkeeping with only a few
		// identifying bytes. Do not infer identity from those opaque records.
		constexpr size_t minimumContentMatchSize = 4096;
		constexpr double minimumMatchingByteRatio = 0.98;
		constexpr double minimumStableRunRatio = 0.50;
		constexpr double ambiguityRatioMargin = 0.002;

		struct CachedBlobContentMatch
		{
			double matchingRatio = 0.0;
			size_t longestMatchingRun = 0;
		};

		std::unordered_map<std::string, std::vector<uint8_t>> gCachedBlobSidecars;

		const std::vector<uint8_t>& LoadCachedBlobSidecar(const std::string& path)
		{
			auto existing = gCachedBlobSidecars.find(path);
			if (existing != gCachedBlobSidecars.end())
				return existing->second;

			std::vector<uint8_t> bytes;
			if (!path.empty())
				ShaderInjectorIO::LoadDXILBlobFromDisk(path, bytes);

			return gCachedBlobSidecars.emplace(path, std::move(bytes)).first->second;
		}

		CachedBlobContentMatch CompareCachedBlobContent(
			const std::vector<uint8_t>& persistedBlob,
			const std::vector<uint8_t>& currentBlob)
		{
			CachedBlobContentMatch match{};
			if (persistedBlob.size() != currentBlob.size() || persistedBlob.size() < minimumContentMatchSize)
				return match;

			size_t matchingBytes = 0;
			size_t currentMatchingRun = 0;

			for (size_t byteIndex = 0; byteIndex < persistedBlob.size(); ++byteIndex)
			{
				if (persistedBlob[byteIndex] != currentBlob[byteIndex])
				{
					currentMatchingRun = 0;
					continue;
				}

				++matchingBytes;
				++currentMatchingRun;
				match.longestMatchingRun = (std::max)(match.longestMatchingRun, currentMatchingRun);
			}

			match.matchingRatio = static_cast<double>(matchingBytes) / static_cast<double>(persistedBlob.size());
			return match;
		}

		CachedBlobContentMatch BestCachedBlobContentMatch(
			const ShaderReplacement::ShaderReplacementDisk& replacement,
			const std::vector<uint8_t>& currentBlob)
		{
			CachedBlobContentMatch bestMatch{};

			auto considerPath = [&](const std::string& path)
			{
				const CachedBlobContentMatch match = CompareCachedBlobContent(LoadCachedBlobSidecar(path), currentBlob);
				if (match.matchingRatio > bestMatch.matchingRatio ||
					(match.matchingRatio == bestMatch.matchingRatio && match.longestMatchingRun > bestMatch.longestMatchingRun))
				{
					bestMatch = match;
				}
			};

			considerPath(replacement.pipelineCachedBlobPath);
			for (const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
				considerPath(pipelineTemplate.pipelineCachedBlobPath);

			return bestMatch;
		}
	}

	void ResetCachedBlobContentLookup()
	{
		gCachedBlobSidecars.clear();
	}

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

	int FindEnabledShaderReplacementByCachedBlobContent(
		const std::vector<uint8_t>& cachedBlob,
		double& outMatchingRatio,
		size_t& outLongestMatchingRun)
	{
		outMatchingRatio = 0.0;
		outLongestMatchingRun = 0;

		if (cachedBlob.size() < minimumContentMatchSize)
			return -1;

		int bestReplacementIndex = -1;
		double secondBestRatio = 0.0;

		for (int replacementIndex = 0; replacementIndex < static_cast<int>(gLoadedShaderReplacements.size()); ++replacementIndex)
		{
			const ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];
			if (!replacement.enabled)
				continue;

			const CachedBlobContentMatch match = BestCachedBlobContentMatch(replacement, cachedBlob);
			const size_t minimumStableRun = static_cast<size_t>(cachedBlob.size() * minimumStableRunRatio);
			if (match.matchingRatio < minimumMatchingByteRatio || match.longestMatchingRun < minimumStableRun)
				continue;

			if (match.matchingRatio > outMatchingRatio)
			{
				secondBestRatio = outMatchingRatio;
				outMatchingRatio = match.matchingRatio;
				outLongestMatchingRun = match.longestMatchingRun;
				bestReplacementIndex = replacementIndex;
			}
			else
			{
				secondBestRatio = (std::max)(secondBestRatio, match.matchingRatio);
			}
		}

		if (bestReplacementIndex < 0)
			return -1;

		// Two nearly identical enabled replacements targeting the same cached PSO
		// are ambiguous. Refuse to guess rather than binding the wrong pipeline.
		if (secondBestRatio > 0.0 && outMatchingRatio - secondBestRatio < ambiguityRatioMargin)
		{
			outMatchingRatio = 0.0;
			outLongestMatchingRun = 0;
			return -1;
		}

		return bestReplacementIndex;
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
