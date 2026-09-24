//HookD3D12ReplacementCreation.cpp
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>

//custom
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "HookD3D12.h"
#include "Hash/Hash.h"
#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderAnalyzer.h"
#include "ShaderTarget/ShaderTarget.h"
#include "StringHelper.h"

namespace HookD3D12
{
	uint64_t GraphicsShaderHashForType(
		const GraphicsPipelineInfo& pipeline,
		ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderTarget::VertexShader:
				return pipeline.vertexShaderHash;
			case ShaderTarget::HullShader:
				return pipeline.hullShaderHash;
			case ShaderTarget::DomainShader:
				return pipeline.domainShaderHash;
			case ShaderTarget::GeometryShader:
				return pipeline.geometryShaderHash;
			case ShaderTarget::PixelShader:
				return pipeline.pixelShaderHash;
			default:
				return 0;
		}
	}

	bool CreateShaderTarget(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		ID3D12PipelineState* pipelineState,
		const GraphicsPipelineInfo* graphicsInfo,
		const PipelineStateInfo* streamInfo,
		const std::string& modifiedShaderId,
		bool generateShaderDisassembly,
		const ShaderAnalysis::ShaderAnalysisDisk* originalShaderAnalysis)
	{
		if (!shaderHash || !shaderBytecode || shaderBytecodeLength == 0)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementCreation->CreateReplacementShaderTemplate: missing shader bytecode");
			return false;
		}

		const bool pipelineContainsRequestedShader =
			(graphicsInfo && GraphicsShaderHashForType(*graphicsInfo, shaderType) == shaderHash) ||
			(streamInfo && StreamPipelineHasShaderHash(*streamInfo, shaderType, shaderHash));

		if (!pipelineContainsRequestedShader)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"HookD3D12ReplacementCreation->CreateReplacementShaderTemplate: refusing mismatched pipeline for " +
				StringHelper::ShaderTypeToString(shaderType) + " " + Hash::FormatHash(shaderHash));

			return false;
		}

		const ULONGLONG creationStartTick = GetTickCount64();
		const std::string hashText = Hash::FormatHash(shaderHash);
		const std::string shaderTypeText = StringHelper::ShaderTypeToString(shaderType);
		const std::string replacementName = "ShaderTarget_" + shaderTypeText + "_" + hashText;
		const std::string replacementDirectory = ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetShaderTargetsDirectory(), replacementName);

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
			ShaderInjectorIO::DirectoryCreate(replacementDirectory);

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementCreation->CreateReplacementShaderTemplate: failed to create directory " + replacementDirectory);
			return false;
		}

		ShaderTarget::ShaderTargetDisk replacement{};
		replacement.schemaVersion = 6;
		//Replacements remain enabled unless the user explicitly disables them.
		//Matching safety is enforced by exact hashes and verified blob content.
		replacement.enabled = true;
		replacement.name = replacementName;
		replacement.shaderType = shaderType;
		replacement.shaderProfile = StringHelper::ShaderProfileForType(shaderType);
		replacement.shaderEntryPoint = "main";
		replacement.originalShaderBytecodeHash = hashText;
		replacement.originalShaderBytecodeLength = std::to_string(shaderBytecodeLength);
		replacement.replacementDirectory = replacementDirectory;
		replacement.originalShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalShaderBytecode" + ShaderInjectorIO::extensionBIN);
		replacement.modifiedShaderId = modifiedShaderId;

		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);

		if (modifiedShader)
			replacement.modifiedShaderBlobPath = modifiedShader->compiledBlobPath;

		if (originalShaderAnalysis && originalShaderAnalysis->succeeded)
		{
			replacement.originalShaderAnalysis = *originalShaderAnalysis;
		}
		else if (!ShaderAnalyzer::Analyze(shaderBytecode, shaderBytecodeLength, replacement.originalShaderAnalysis))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"HookD3D12ReplacementCreation->CreateReplacementShaderTemplate: shader analysis unavailable for " +
				replacementName + ": " + replacement.originalShaderAnalysis.error);
		}

		replacement.jsonPath = ShaderInjectorIO::JoinPath(replacementDirectory, "ShaderTarget" + ShaderInjectorIO::extensionJSON);
		replacement.sourceList = sourceList;
		replacement.pipelineIndex = std::to_string(pipelineIndex);
		replacement.pipelineStateType = "GraphicsPipelineStateDesc";

		if (sourceList == "Stream")
			replacement.pipelineStateType = "PipelineStateStream";

		replacement.psoPointer = StringHelper::PointerToString(pipelineState);

		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;

		if (GetPipelineCachedBlobInfo(pipelineState, cachedBlobHash, cachedBlobSize, &cachedBlob))
		{
			replacement.pipelineCachedBlobHash = Hash::FormatHash(cachedBlobHash);
			replacement.pipelineCachedBlobLength = std::to_string(cachedBlobSize);
			replacement.pipelineCachedBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, "OriginalPipelineCachedBlob" + ShaderInjectorIO::extensionBIN);
		}

		replacement.targetSubobjectType = std::to_string((UINT)SubobjectTypeForShaderType(shaderType));

		if (graphicsInfo)
		{
			FillCommonReplacementHashes(replacement, graphicsInfo->vertexShaderHash, graphicsInfo->pixelShaderHash, 0, graphicsInfo->geometryShaderHash, graphicsInfo->hullShaderHash, graphicsInfo->domainShaderHash);
			FillGraphicsReplacementPortableState(replacement, *graphicsInfo);

			std::vector<uint8_t> rootSignatureBlob;
			uint64_t rootSignatureHash = 0;

			if (GetRootSignatureBlob(graphicsInfo->originalDescription.pRootSignature, rootSignatureBlob, rootSignatureHash))
			{
				replacement.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
				replacement.rootSignatureLength = std::to_string(rootSignatureBlob.size());
			}
		}
		else if (streamInfo)
		{
			FillCommonReplacementHashes(replacement, streamInfo->vertexShaderHash, streamInfo->pixelShaderHash, streamInfo->computeShaderHash, streamInfo->geometryShaderHash, streamInfo->hullShaderHash, streamInfo->domainShaderHash);
			FillStreamReplacementPortableStateFromBlob(replacement, *streamInfo);

			if (!streamInfo->streamBlob.empty())
				replacement.pipelineStreamBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "PipelineStateStream" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->streamBlob.empty())
				replacement.pipelineStreamMetadataPath = ShaderInjectorIO::JoinPath(replacementDirectory, "PipelineStateStreamMetadata" + ShaderInjectorIO::extensionJSON);

			std::vector<uint8_t> rootSignatureBlob;
			uint64_t rootSignatureHash = 0;

			if (GetRootSignatureBlob(streamInfo->rootSignature, rootSignatureBlob, rootSignatureHash))
			{
				replacement.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
				replacement.rootSignatureLength = std::to_string(rootSignatureBlob.size());
				replacement.rootSignatureBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "RootSignatureBlob" + ShaderInjectorIO::extensionBIN);
			}

			if (!streamInfo->vertexShaderBytecode.empty())
				replacement.vertexShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalVertexShaderBytecode" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->pixelShaderBytecode.empty())
				replacement.pixelShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalPixelShaderBytecode" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->computeShaderBytecode.empty())
				replacement.computeShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalComputeShaderBytecode" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->geometryShaderBytecode.empty())
				replacement.geometryShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalGeometryShaderBytecode" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->hullShaderBytecode.empty())
				replacement.hullShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalHullShaderBytecode" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->domainShaderBytecode.empty())
				replacement.domainShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalDomainShaderBytecode" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->amplificationShaderBytecode.empty())
				replacement.amplificationShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalAmplificationShaderBytecode" + ShaderInjectorIO::extensionBIN);

			if (!streamInfo->meshShaderBytecode.empty())
				replacement.meshShaderBlobPath = ShaderInjectorIO::JoinPath(replacementDirectory, "OriginalMeshShaderBytecode" + ShaderInjectorIO::extensionBIN);
		}

		bool ok = true;
		ok = ShaderInjectorIO::WriteBinaryFile(replacement.originalShaderBlobPath, shaderBytecode, shaderBytecodeLength) && ok;

		if (!cachedBlob.empty() && !replacement.pipelineCachedBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.pipelineCachedBlobPath, cachedBlob.data(), cachedBlob.size()) && ok;

		if (streamInfo && !streamInfo->streamBlob.empty() && !replacement.pipelineStreamBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.pipelineStreamBlobPath, streamInfo->streamBlob.data(), streamInfo->streamBlob.size()) && ok;

		if (streamInfo && !replacement.pipelineStreamMetadataPath.empty())
		{
			ShaderTarget::ShaderPipelineStreamMetadataDisk metadata = BuildPipelineStreamMetadata(*streamInfo);
			ok = ShaderTarget::WritePipelineStreamMetadataJson(replacement.pipelineStreamMetadataPath, metadata) && ok;
		}

		if (streamInfo && !replacement.rootSignatureBlobPath.empty())
		{
			std::vector<uint8_t> rootSignatureBlob;
			uint64_t rootSignatureHash = 0;

			if (GetRootSignatureBlob(streamInfo->rootSignature, rootSignatureBlob, rootSignatureHash))
				ok = ShaderInjectorIO::WriteBinaryFile(replacement.rootSignatureBlobPath, rootSignatureBlob.data(), rootSignatureBlob.size()) && ok;
		}

		if (streamInfo && !streamInfo->vertexShaderBytecode.empty() && !replacement.vertexShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.vertexShaderBlobPath, streamInfo->vertexShaderBytecode.data(), streamInfo->vertexShaderBytecode.size()) && ok;

		if (streamInfo && !streamInfo->pixelShaderBytecode.empty() && !replacement.pixelShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.pixelShaderBlobPath, streamInfo->pixelShaderBytecode.data(), streamInfo->pixelShaderBytecode.size()) && ok;

		if (streamInfo && !streamInfo->computeShaderBytecode.empty() && !replacement.computeShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.computeShaderBlobPath, streamInfo->computeShaderBytecode.data(), streamInfo->computeShaderBytecode.size()) && ok;

		if (streamInfo && !streamInfo->geometryShaderBytecode.empty() && !replacement.geometryShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.geometryShaderBlobPath, streamInfo->geometryShaderBytecode.data(), streamInfo->geometryShaderBytecode.size()) && ok;

		if (streamInfo && !streamInfo->hullShaderBytecode.empty() && !replacement.hullShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.hullShaderBlobPath, streamInfo->hullShaderBytecode.data(), streamInfo->hullShaderBytecode.size()) && ok;

		if (streamInfo && !streamInfo->domainShaderBytecode.empty() && !replacement.domainShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.domainShaderBlobPath, streamInfo->domainShaderBytecode.data(), streamInfo->domainShaderBytecode.size()) && ok;

		if (streamInfo && !streamInfo->amplificationShaderBytecode.empty() && !replacement.amplificationShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.amplificationShaderBlobPath, streamInfo->amplificationShaderBytecode.data(), streamInfo->amplificationShaderBytecode.size()) && ok;

		if (streamInfo && !streamInfo->meshShaderBytecode.empty() && !replacement.meshShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.meshShaderBlobPath, streamInfo->meshShaderBytecode.data(), streamInfo->meshShaderBytecode.size()) && ok;

		if (generateShaderDisassembly)
			ok = ShaderInjectorIO::GenerateShaderTextDXIL(replacement.originalShaderBlobPath) && ok;

		ok = ShaderTarget::WriteShaderTargetJson(replacement) && ok;

		if (!ok)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementCreation->CreateReplacementShaderTemplate: failed to write one or more files for " + replacementName);
			return false;
		}

		const ULONGLONG creationDurationMs = GetTickCount64() - creationStartTick;
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12ReplacementCreation->CreateReplacementShaderTemplate: Created replacement shader template: " + replacement.jsonPath + " durationMs=" + std::to_string(creationDurationMs));

		if (gLoadedShaderTargetsOnce)
		{
			const auto existingTarget = std::find_if(gLoadedShaderTargets.begin(), gLoadedShaderTargets.end(),
													 [&replacement](const ShaderTarget::ShaderTargetDisk& loadedTarget)
													 {
														 return loadedTarget.jsonPath == replacement.jsonPath ||
																(loadedTarget.shaderType == replacement.shaderType &&
																 loadedTarget.originalShaderBytecodeHash == replacement.originalShaderBytecodeHash);
													 });

			if (existingTarget == gLoadedShaderTargets.end())
			{
				std::vector<uint8_t> compiledReplacementBlob;

				if (modifiedShader && modifiedShader->enabled && modifiedShader->shaderType == replacement.shaderType)
					compiledReplacementBlob = modifiedShader->compiledBlob;

				gLoadedShaderTargets.push_back(replacement);
				gLoadedShaderTargetBlobs.push_back(std::move(compiledReplacementBlob));
			}
		}

		QueueShaderTargetApplyWork();
		return true;
	}

	bool CreateShaderTargetForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		GraphicsPipelineInfo& pipeline,
		const std::string& modifiedShaderId,
		bool generateShaderDisassembly,
		const ShaderAnalysis::ShaderAnalysisDisk* originalShaderAnalysis)
	{
		return CreateShaderTarget(sourceList, pipelineIndex, shaderType, shaderHash, shaderBytecodeLength, shaderBytecode, pipeline.pipelineState, &pipeline, nullptr, modifiedShaderId, generateShaderDisassembly, originalShaderAnalysis);
	}

	bool CreateShaderTargetForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		PipelineStateInfo& pipeline,
		const std::string& modifiedShaderId,
		bool generateShaderDisassembly,
		const ShaderAnalysis::ShaderAnalysisDisk* originalShaderAnalysis)
	{
		return CreateShaderTarget(sourceList, pipelineIndex, shaderType, shaderHash, shaderBytecodeLength, shaderBytecode, pipeline.pipelineState, nullptr, &pipeline, modifiedShaderId, generateShaderDisassembly, originalShaderAnalysis);
	}
} //namespace HookD3D12
