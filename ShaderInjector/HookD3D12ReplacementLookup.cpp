//HookD3D12ReplacementLookup.cpp
#include "HookD3D12ReplacementLookup.h"

#include <algorithm>
#include <unordered_map>

//custom
#include "Hash.h"
#include "DatabaseShaderTargets.h"
#include "HookD3D12ReplacementTemplates.h"
#include "ShaderDiscovery.h"
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

		struct CapturedShaderKey
		{
			uint64_t hash = 0;
			ShaderTarget::ShaderType type = ShaderTarget::Unknown;

			bool operator==(const CapturedShaderKey& other) const
			{
				return hash == other.hash && type == other.type;
			}
		};

		struct CapturedShaderKeyHasher
		{
			size_t operator()(const CapturedShaderKey& key) const
			{
				return static_cast<size_t>(key.hash ^ (static_cast<uint64_t>(key.type) << 57));
			}
		};

		struct CapturedShaderLocation
		{
			bool streamPipeline = false;
			size_t pipelineIndex = 0;
		};

		std::unordered_map<CapturedShaderKey, CapturedShaderLocation, CapturedShaderKeyHasher> gCapturedShaderLocations;
		size_t gIndexedGraphicsPipelineCount = 0;
		size_t gIndexedStreamPipelineCount = 0;

		uint64_t GraphicsShaderHash(const GraphicsPipelineInfo& pipeline, ShaderTarget::ShaderType shaderType)
		{
			switch (shaderType)
			{
				case ShaderTarget::VertexShader: return pipeline.vsHash;
				case ShaderTarget::PixelShader: return pipeline.psHash;
				case ShaderTarget::GeometryShader: return pipeline.gsHash;
				case ShaderTarget::HullShader: return pipeline.hsHash;
				case ShaderTarget::DomainShader: return pipeline.dsHash;
				default: return 0;
			}
		}

		const std::vector<uint8_t>& GraphicsShaderBytecode(const GraphicsPipelineInfo& pipeline, ShaderTarget::ShaderType shaderType)
		{
			static const std::vector<uint8_t> emptyBytecode;
			switch (shaderType)
			{
				case ShaderTarget::VertexShader: return pipeline.vsBytecode;
				case ShaderTarget::PixelShader: return pipeline.psBytecode;
				case ShaderTarget::GeometryShader: return pipeline.gsBytecode;
				case ShaderTarget::HullShader: return pipeline.hsBytecode;
				case ShaderTarget::DomainShader: return pipeline.dsBytecode;
				default: return emptyBytecode;
			}
		}

		const std::vector<uint8_t>& StreamShaderBytecode(const PipelineStateInfo& pipeline, ShaderTarget::ShaderType shaderType)
		{
			static const std::vector<uint8_t> emptyBytecode;
			switch (shaderType)
			{
				case ShaderTarget::VertexShader: return pipeline.vsBytecode;
				case ShaderTarget::PixelShader: return pipeline.psBytecode;
				case ShaderTarget::ComputeShader: return pipeline.csBytecode;
				case ShaderTarget::GeometryShader: return pipeline.gsBytecode;
				case ShaderTarget::HullShader: return pipeline.hsBytecode;
				case ShaderTarget::DomainShader: return pipeline.dsBytecode;
				default: return emptyBytecode;
			}
		}

		void RebuildCapturedShaderLocationIndex()
		{
			gCapturedShaderLocations.clear();
			const ShaderTarget::ShaderType graphicsTypes[] =
			{
				ShaderTarget::VertexShader,
				ShaderTarget::PixelShader,
				ShaderTarget::GeometryShader,
				ShaderTarget::HullShader,
				ShaderTarget::DomainShader,
			};
			const ShaderTarget::ShaderType streamTypes[] =
			{
				ShaderTarget::VertexShader,
				ShaderTarget::PixelShader,
				ShaderTarget::ComputeShader,
				ShaderTarget::GeometryShader,
				ShaderTarget::HullShader,
				ShaderTarget::DomainShader,
			};

			for (size_t pipelineIndex = 0; pipelineIndex < gGraphicsPipelines.size(); ++pipelineIndex)
			{
				for (ShaderTarget::ShaderType shaderType : graphicsTypes)
				{
					const uint64_t shaderHash = GraphicsShaderHash(gGraphicsPipelines[pipelineIndex], shaderType);
					if (shaderHash != 0)
						gCapturedShaderLocations.emplace(CapturedShaderKey{ shaderHash, shaderType }, CapturedShaderLocation{ false, pipelineIndex });
				}
			}

			for (size_t pipelineIndex = 0; pipelineIndex < gPipelineStates.size(); ++pipelineIndex)
			{
				for (ShaderTarget::ShaderType shaderType : streamTypes)
				{
					const uint64_t shaderHash = StreamShaderHashForType(gPipelineStates[pipelineIndex], shaderType);
					if (shaderHash != 0)
						gCapturedShaderLocations[CapturedShaderKey{ shaderHash, shaderType }] = CapturedShaderLocation{ true, pipelineIndex };
				}
			}

			gIndexedGraphicsPipelineCount = gGraphicsPipelines.size();
			gIndexedStreamPipelineCount = gPipelineStates.size();
		}

		const std::vector<uint8_t>* FindCapturedShaderBytecode(
			uint64_t shaderHash,
			ShaderTarget::ShaderType shaderType,
			bool& outStreamPipeline)
		{
			outStreamPipeline = false;
			if (gIndexedGraphicsPipelineCount != gGraphicsPipelines.size() ||
				gIndexedStreamPipelineCount != gPipelineStates.size())
			{
				RebuildCapturedShaderLocationIndex();
			}

			const auto location = gCapturedShaderLocations.find(CapturedShaderKey{ shaderHash, shaderType });
			if (location == gCapturedShaderLocations.end())
				return nullptr;

			outStreamPipeline = location->second.streamPipeline;
			if (outStreamPipeline)
			{
				if (location->second.pipelineIndex >= gPipelineStates.size())
					return nullptr;
				return &StreamShaderBytecode(gPipelineStates[location->second.pipelineIndex], shaderType);
			}

			if (location->second.pipelineIndex >= gGraphicsPipelines.size())
				return nullptr;
			return &GraphicsShaderBytecode(gGraphicsPipelines[location->second.pipelineIndex], shaderType);
		}

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

		bool CachedBlobLengthMatches(const std::string& serializedLength, size_t currentBlobSize)
		{
			if (serializedLength.empty())
				return false;

			char* parseEnd = nullptr;
			const unsigned long long parsedLength = _strtoui64(serializedLength.c_str(), &parseEnd, 10);
			return parseEnd != serializedLength.c_str() && *parseEnd == '\0' && parsedLength == currentBlobSize;
		}

		CachedBlobContentMatch BestCachedBlobContentMatch(
			const ShaderTarget::ShaderTargetDisk& replacement,
			const std::vector<uint8_t>& currentBlob)
		{
			CachedBlobContentMatch bestMatch{};

			auto considerPath = [&](const std::string& path, const std::string& serializedLength)
			{
				// Driver cache sidecars can be large and a target may contain many templates.
				// Length metadata lets us reject almost every candidate without loading it
				// from disk or comparing it byte-by-byte on the render thread.
				if (!CachedBlobLengthMatches(serializedLength, currentBlob.size()))
					return;

				const CachedBlobContentMatch match = CompareCachedBlobContent(LoadCachedBlobSidecar(path), currentBlob);
				if (match.matchingRatio > bestMatch.matchingRatio ||
					(match.matchingRatio == bestMatch.matchingRatio && match.longestMatchingRun > bestMatch.longestMatchingRun))
				{
					bestMatch = match;
				}
			};

			considerPath(replacement.pipelineCachedBlobPath, replacement.pipelineCachedBlobLength);
			for (const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
				considerPath(pipelineTemplate.pipelineCachedBlobPath, pipelineTemplate.pipelineCachedBlobLength);

			return bestMatch;
		}
	}

	void ResetCachedBlobContentLookup()
	{
		gCachedBlobSidecars.clear();
		gCapturedShaderLocations.clear();
		gIndexedGraphicsPipelineCount = 0;
		gIndexedStreamPipelineCount = 0;
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

	bool ReplacementHasCachedBlobHash(const ShaderTarget::ShaderTargetDisk& replacement, uint64_t cachedBlobHash)
	{
		if (!cachedBlobHash)
			return false;

		if (Hash::ParseHashText(replacement.pipelineCachedBlobHash) == cachedBlobHash)
			return true;

		for (const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			if (Hash::ParseHashText(pipelineTemplate.pipelineCachedBlobHash) == cachedBlobHash)
				return true;
		}

		return false;
	}

	int FindEnabledShaderTargetByCachedBlob(uint64_t cachedBlobHash)
	{
		if (!cachedBlobHash)
			return -1;

		for (int i = 0; i < (int)gLoadedShaderTargets.size(); i++)
		{
			const ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[i];

			if (!IsShaderTargetEffectivelyEnabled(replacement))
				continue;

			if (ReplacementHasCachedBlobHash(replacement, cachedBlobHash))
				return i;
		}

		return -1;
	}

	int FindEnabledShaderTargetByCachedBlobContent(
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

		for (int replacementIndex = 0; replacementIndex < static_cast<int>(gLoadedShaderTargets.size()); ++replacementIndex)
		{
			const ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
			if (!IsShaderTargetEffectivelyEnabled(replacement))
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

	bool GraphicsPipelineMatchesReplacementTemplate(const GraphicsPipelineInfo& pipeline, const ShaderTarget::ShaderTargetDisk& replacement)
	{
		return ReplacementHashMatches(pipeline.vsHash, replacement.vsHash) &&
			ReplacementHashMatches(pipeline.psHash, replacement.psHash) &&
			ReplacementHashMatches(pipeline.gsHash, replacement.gsHash) &&
			ReplacementHashMatches(pipeline.hsHash, replacement.hsHash) &&
			ReplacementHashMatches(pipeline.dsHash, replacement.dsHash);
	}

	bool StreamPipelineMatchesReplacementTemplate(const PipelineStateInfo& pipeline, const ShaderTarget::ShaderTargetDisk& replacement)
	{
		return ReplacementHashMatches(pipeline.vsHash, replacement.vsHash) &&
			ReplacementHashMatches(pipeline.psHash, replacement.psHash) &&
			ReplacementHashMatches(pipeline.csHash, replacement.csHash) &&
			ReplacementHashMatches(pipeline.gsHash, replacement.gsHash) &&
			ReplacementHashMatches(pipeline.hsHash, replacement.hsHash) &&
			ReplacementHashMatches(pipeline.dsHash, replacement.dsHash);
	}

	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubobjectTypeForShaderType(ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderTarget::VertexShader:   return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
			case ShaderTarget::PixelShader:    return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
			case ShaderTarget::GeometryShader: return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS;
			case ShaderTarget::HullShader:     return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS;
			case ShaderTarget::DomainShader:   return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS;
			case ShaderTarget::ComputeShader:  return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS;
			default: return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID;
		}
	}

	int FindEnabledShaderTarget(uint64_t shaderHash, ShaderTarget::ShaderType shaderType)
	{
		if (!shaderHash)
			return -1;

		for (int i = 0; i < (int)gLoadedShaderTargets.size(); i++)
		{
			const ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[i];

			if (!IsShaderTargetEffectivelyEnabled(replacement) || replacement.shaderType != shaderType)
				continue;

			if (Hash::ParseHashText(replacement.originalShaderBytecodeHash) == shaderHash)
				return i;

			for (const std::string& aliasHash : replacement.shaderBytecodeHashAliases)
			{
				if (Hash::ParseHashText(aliasHash) == shaderHash)
				{
					bool streamPipeline = false;
					FindCapturedShaderBytecode(shaderHash, shaderType, streamPipeline);
					if (streamPipeline)
						PersistStreamPipelineTemplatesForShaderAlias(gLoadedShaderTargets[i], shaderType, shaderHash);
					return i;
				}
			}
		}

		bool streamPipeline = false;
		const std::vector<uint8_t>* shaderBytecode = FindCapturedShaderBytecode(shaderHash, shaderType, streamPipeline);
		if (!shaderBytecode || shaderBytecode->empty())
			return -1;

		const int discoveredReplacementIndex = ShaderDiscovery::DiscoverEnabledReplacement(
			shaderHash,
			shaderType,
			*shaderBytecode,
			gLoadedShaderTargets);
		if (discoveredReplacementIndex < 0)
			return -1;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[discoveredReplacementIndex];
		if (!IsShaderTargetEffectivelyEnabled(replacement))
			return -1;

		ShaderDiscovery::PersistShaderHashAlias(replacement, shaderHash);
		if (streamPipeline)
			PersistStreamPipelineTemplatesForShaderAlias(replacement, shaderType, shaderHash);
		return discoveredReplacementIndex;
	}

	bool ReplacementStillEnabled(const std::string& replacementName, uint64_t shaderHash, ShaderTarget::ShaderType shaderType)
	{
		const int replacementIndex = FindEnabledShaderTarget(shaderHash, shaderType);

		if (replacementIndex < 0)
			return false;

		return gLoadedShaderTargets[replacementIndex].name == replacementName;
	}
}
