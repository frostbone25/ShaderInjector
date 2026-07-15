//HookD3D12ReplacementTemplates.cpp
#include "HookD3D12ReplacementTemplates.h"

#include <algorithm>

//custom
#include "Hash.h"
#include "HookD3D12PipelineUtils.h"
#include "HookD3D12ReplacementLookup.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"

namespace HookD3D12
{
	bool LoadPersistedShaderBlob(const std::string& path, std::vector<uint8_t>& bytecode, uint64_t& hash, SIZE_T& size)
	{
		bytecode.clear();
		hash = 0;
		size = 0;

		if (path.empty())
			return true;

		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(path, bytecode))
			return false;

		size = bytecode.size();
		hash = bytecode.empty() ? 0 : Hash::HashMemory(bytecode.data(), bytecode.size());
		return true;
	}

	bool PipelineStreamContainsSubobjectType(const std::vector<uint8_t>& streamBlob, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType)
	{
		const uint8_t* ptr = streamBlob.data();
		const uint8_t* end = ptr + streamBlob.size();

		while (ptr < end)
		{
			if (ptr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
				return false;

			const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			const UINT typeIndex = static_cast<UINT>(type);

			if (typeIndex >= ARRAYSIZE(kSubobjectSizes))
				return false;

			const size_t subobjectSize = kSubobjectSizes[typeIndex];

			if (subobjectSize == 0 || ptr + subobjectSize > end)
				return false;

			if (type == targetType)
				return true;

			ptr += subobjectSize;
		}

		return false;
	}

	void BackfillReplacementPortableMetadataFromSidecars(ShaderTarget::ShaderTargetDisk& replacement)
	{
		if (!replacement.rootSignatureBlobPath.empty())
		{
			std::vector<uint8_t> rootBlob;
			if (ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.rootSignatureBlobPath, rootBlob) && !rootBlob.empty())
			{
				replacement.rootSignatureLength = std::to_string(rootBlob.size());
				if (replacement.rootSignatureHash.empty())
					replacement.rootSignatureHash = Hash::FormatHash(Hash::HashMemory(rootBlob.data(), rootBlob.size()));
			}
		}

		if (replacement.pipelineStreamBlobPath.empty())
			return;

		PipelineStateInfo persistedPipeline{};
		if (!LoadPersistedStreamTemplateFromReplacement(replacement, persistedPipeline))
			return;

		FillStreamReplacementPortableStateFromBlob(replacement, persistedPipeline);
	}

	bool LoadPersistedStreamTemplateFromReplacement(const ShaderTarget::ShaderTargetDisk& replacement, PipelineStateInfo& outPipeline)
	{
		outPipeline = PipelineStateInfo{};

		if (replacement.pipelineStreamBlobPath.empty())
			return false;

		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.pipelineStreamBlobPath, outPipeline.streamBlob))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementTemplates->LoadPersistedStreamTemplateFromReplacement: missing stream blob for " + replacement.name);
			return false;
		}

		if (!LoadPersistedShaderBlob(replacement.vertexShaderBlobPath, outPipeline.vsBytecode, outPipeline.vsHash, outPipeline.vsSize) ||
			!LoadPersistedShaderBlob(replacement.pixelShaderBlobPath, outPipeline.psBytecode, outPipeline.psHash, outPipeline.psSize) ||
			!LoadPersistedShaderBlob(replacement.computeShaderBlobPath, outPipeline.csBytecode, outPipeline.csHash, outPipeline.csSize) ||
			!LoadPersistedShaderBlob(replacement.geometryShaderBlobPath, outPipeline.gsBytecode, outPipeline.gsHash, outPipeline.gsSize) ||
			!LoadPersistedShaderBlob(replacement.hullShaderBlobPath, outPipeline.hsBytecode, outPipeline.hsHash, outPipeline.hsSize) ||
			!LoadPersistedShaderBlob(replacement.domainShaderBlobPath, outPipeline.dsBytecode, outPipeline.dsHash, outPipeline.dsSize) ||
			!LoadPersistedShaderBlob(replacement.amplificationShaderBlobPath, outPipeline.asBytecode, outPipeline.asHash, outPipeline.asSize) ||
			!LoadPersistedShaderBlob(replacement.meshShaderBlobPath, outPipeline.msBytecode, outPipeline.msHash, outPipeline.msSize))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementTemplates->LoadPersistedStreamTemplateFromReplacement: missing original shader stage blob for " + replacement.name);
			return false;
		}

		const bool streamRequiresAmplificationShader = PipelineStreamContainsSubobjectType(outPipeline.streamBlob, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS);
		const bool streamRequiresMeshShader = PipelineStreamContainsSubobjectType(outPipeline.streamBlob, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS);

		if ((streamRequiresAmplificationShader && outPipeline.asBytecode.empty()) ||
			(streamRequiresMeshShader && outPipeline.msBytecode.empty()))
		{
			ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12ReplacementTemplates->LoadPersistedStreamTemplateFromReplacement: persisted stream template is missing AS/MS shader sidecars for " + replacement.name + "; recreate this shader target from a fresh capture");
			return false;
		}

		outPipeline.isCompute = !outPipeline.csBytecode.empty();
		outPipeline.isGraphics = !outPipeline.vsBytecode.empty() || !outPipeline.psBytecode.empty() || !outPipeline.gsBytecode.empty() || !outPipeline.hsBytecode.empty() || !outPipeline.dsBytecode.empty() || !outPipeline.asBytecode.empty() || !outPipeline.msBytecode.empty();

		if (!replacement.pipelineStreamMetadataPath.empty())
		{
			ShaderTarget::ShaderPipelineStreamMetadataDisk metadata{};

			if (!ShaderTarget::LoadPipelineStreamMetadataJson(replacement.pipelineStreamMetadataPath, metadata))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementTemplates->LoadPersistedStreamTemplateFromReplacement: missing stream metadata for " + replacement.name);
				return false;
			}

			ApplyPipelineStreamMetadata(metadata, outPipeline);
		}

		return true;
	}

	uint64_t StreamShaderHashForType(const PipelineStateInfo& pipeline, ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderTarget::VertexShader: return pipeline.vsHash;
			case ShaderTarget::HullShader: return pipeline.hsHash;
			case ShaderTarget::DomainShader: return pipeline.dsHash;
			case ShaderTarget::GeometryShader: return pipeline.gsHash;
			case ShaderTarget::PixelShader: return pipeline.psHash;
			case ShaderTarget::ComputeShader: return pipeline.csHash;
			default: return 0;
		}
	}

	bool StreamPipelineHasShaderHash(const PipelineStateInfo& pipeline, ShaderTarget::ShaderType shaderType, uint64_t shaderHash)
	{
		return shaderHash != 0 && StreamShaderHashForType(pipeline, shaderType) == shaderHash;
	}

	void FillPipelineTemplateCommonState(ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate, const PipelineStateInfo& pipeline)
	{
		pipelineTemplate.vsHash = pipeline.vsHash ? Hash::FormatHash(pipeline.vsHash) : "";
		pipelineTemplate.psHash = pipeline.psHash ? Hash::FormatHash(pipeline.psHash) : "";
		pipelineTemplate.csHash = pipeline.csHash ? Hash::FormatHash(pipeline.csHash) : "";
		pipelineTemplate.gsHash = pipeline.gsHash ? Hash::FormatHash(pipeline.gsHash) : "";
		pipelineTemplate.hsHash = pipeline.hsHash ? Hash::FormatHash(pipeline.hsHash) : "";
		pipelineTemplate.dsHash = pipeline.dsHash ? Hash::FormatHash(pipeline.dsHash) : "";
		pipelineTemplate.asHash = pipeline.asHash ? Hash::FormatHash(pipeline.asHash) : "";
		pipelineTemplate.msHash = pipeline.msHash ? Hash::FormatHash(pipeline.msHash) : "";
		pipelineTemplate.vsLength = pipeline.vsSize ? std::to_string((size_t)pipeline.vsSize) : "";
		pipelineTemplate.psLength = pipeline.psSize ? std::to_string((size_t)pipeline.psSize) : "";
		pipelineTemplate.csLength = pipeline.csSize ? std::to_string((size_t)pipeline.csSize) : "";
		pipelineTemplate.gsLength = pipeline.gsSize ? std::to_string((size_t)pipeline.gsSize) : "";
		pipelineTemplate.hsLength = pipeline.hsSize ? std::to_string((size_t)pipeline.hsSize) : "";
		pipelineTemplate.dsLength = pipeline.dsSize ? std::to_string((size_t)pipeline.dsSize) : "";
		pipelineTemplate.asLength = pipeline.asSize ? std::to_string((size_t)pipeline.asSize) : "";
		pipelineTemplate.msLength = pipeline.msSize ? std::to_string((size_t)pipeline.msSize) : "";
		pipelineTemplate.inputLayoutElementCount = std::to_string(pipeline.inputElements.size());
		pipelineTemplate.inputLayoutSignature = InputLayoutSignature(pipeline.inputElements);
		pipelineTemplate.streamOutputDeclarationCount = std::to_string(pipeline.soDeclarations.size());
		pipelineTemplate.streamOutputSignature = StreamOutputSignature(pipeline.soDeclarations, pipeline.soStrides);
		pipelineTemplate.pipelineStreamLength = pipeline.streamBlob.empty() ? "" : std::to_string(pipeline.streamBlob.size());
		pipelineTemplate.pipelineStreamSubobjectTypes = PipelineStreamSubobjectTypeSignature(pipeline.streamBlob);
	}

	ShaderTarget::ShaderTargetDisk ReplacementWithPipelineTemplate(const ShaderTarget::ShaderTargetDisk& replacement, const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate)
	{
		ShaderTarget::ShaderTargetDisk templateReplacement = replacement;
		templateReplacement.name = replacement.name + "/" + pipelineTemplate.name;
		templateReplacement.pipelineIndex = pipelineTemplate.pipelineIndex;
		templateReplacement.psoPointer = pipelineTemplate.psoPointer;
		templateReplacement.pipelineCachedBlobHash = pipelineTemplate.pipelineCachedBlobHash;
		templateReplacement.pipelineCachedBlobLength = pipelineTemplate.pipelineCachedBlobLength;
		templateReplacement.pipelineCachedBlobPath = pipelineTemplate.pipelineCachedBlobPath;
		templateReplacement.pipelineStreamBlobPath = pipelineTemplate.pipelineStreamBlobPath;
		templateReplacement.pipelineStreamMetadataPath = pipelineTemplate.pipelineStreamMetadataPath;
		templateReplacement.rootSignatureBlobPath = pipelineTemplate.rootSignatureBlobPath;
		templateReplacement.rootSignatureHash = pipelineTemplate.rootSignatureHash;
		templateReplacement.rootSignatureLength = pipelineTemplate.rootSignatureLength;
		templateReplacement.vertexShaderBlobPath = pipelineTemplate.vertexShaderBlobPath;
		templateReplacement.pixelShaderBlobPath = pipelineTemplate.pixelShaderBlobPath;
		templateReplacement.computeShaderBlobPath = pipelineTemplate.computeShaderBlobPath;
		templateReplacement.geometryShaderBlobPath = pipelineTemplate.geometryShaderBlobPath;
		templateReplacement.hullShaderBlobPath = pipelineTemplate.hullShaderBlobPath;
		templateReplacement.domainShaderBlobPath = pipelineTemplate.domainShaderBlobPath;
		templateReplacement.amplificationShaderBlobPath = pipelineTemplate.amplificationShaderBlobPath;
		templateReplacement.meshShaderBlobPath = pipelineTemplate.meshShaderBlobPath;
		templateReplacement.vsHash = pipelineTemplate.vsHash;
		templateReplacement.psHash = pipelineTemplate.psHash;
		templateReplacement.csHash = pipelineTemplate.csHash;
		templateReplacement.gsHash = pipelineTemplate.gsHash;
		templateReplacement.hsHash = pipelineTemplate.hsHash;
		templateReplacement.dsHash = pipelineTemplate.dsHash;
		templateReplacement.asHash = pipelineTemplate.asHash;
		templateReplacement.msHash = pipelineTemplate.msHash;
		templateReplacement.vsLength = pipelineTemplate.vsLength;
		templateReplacement.psLength = pipelineTemplate.psLength;
		templateReplacement.csLength = pipelineTemplate.csLength;
		templateReplacement.gsLength = pipelineTemplate.gsLength;
		templateReplacement.hsLength = pipelineTemplate.hsLength;
		templateReplacement.dsLength = pipelineTemplate.dsLength;
		templateReplacement.asLength = pipelineTemplate.asLength;
		templateReplacement.msLength = pipelineTemplate.msLength;
		templateReplacement.pipelineStreamLength = pipelineTemplate.pipelineStreamLength;
		templateReplacement.pipelineStreamSubobjectTypes = pipelineTemplate.pipelineStreamSubobjectTypes;
		templateReplacement.inputLayoutElementCount = pipelineTemplate.inputLayoutElementCount;
		templateReplacement.inputLayoutSignature = pipelineTemplate.inputLayoutSignature;
		templateReplacement.streamOutputDeclarationCount = pipelineTemplate.streamOutputDeclarationCount;
		templateReplacement.streamOutputSignature = pipelineTemplate.streamOutputSignature;
		return templateReplacement;
	}

	SIZE_T CountMatchingBytes(const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs)
	{
		const SIZE_T count = lhs.size() < rhs.size() ? lhs.size() : rhs.size();
		SIZE_T matches = 0;
		for (SIZE_T i = 0; i < count; ++i)
		{
			if (lhs[i] == rhs[i])
				matches++;
		}
		return matches;
	}

	bool WriteStreamPipelineTemplateVariant(ShaderTarget::ShaderTargetDisk& replacement, const PipelineStateInfo& pipeline, int pipelineIndex, int templateIndex, bool& ok)
	{
		if (pipeline.streamBlob.empty())
			return false;

		const std::string prefix = StringHelper::Format("PipelineTemplate_%03d", templateIndex);

		ShaderTarget::ShaderPipelineTemplateDisk pipelineTemplate{};
		pipelineTemplate.name = prefix;
		pipelineTemplate.sourceList = "Stream";
		pipelineTemplate.pipelineIndex = std::to_string(pipelineIndex);
		pipelineTemplate.psoPointer = StringHelper::PointerToString(pipeline.pipelineState);
		pipelineTemplate.pipelineStreamBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_PipelineStateStream" + ShaderInjectorIO::extensionBIN);
		pipelineTemplate.pipelineStreamMetadataPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_PipelineStateStreamMetadata" + ShaderInjectorIO::extensionJSON);
		FillPipelineTemplateCommonState(pipelineTemplate, pipeline);

		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;
		if (GetPipelineCachedBlobInfo(pipeline.pipelineState, cachedBlobHash, cachedBlobSize, &cachedBlob))
		{
			pipelineTemplate.pipelineCachedBlobHash = Hash::FormatHash(cachedBlobHash);
			pipelineTemplate.pipelineCachedBlobLength = std::to_string(cachedBlobSize);
			pipelineTemplate.pipelineCachedBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalPipelineCachedBlob" + ShaderInjectorIO::extensionBIN);
		}

		std::vector<uint8_t> rootSignatureBlob;
		uint64_t rootSignatureHash = 0;
		if (GetRootSignatureBlob(pipeline.rootSignature, rootSignatureBlob, rootSignatureHash))
		{
			pipelineTemplate.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
			pipelineTemplate.rootSignatureLength = std::to_string(rootSignatureBlob.size());
			pipelineTemplate.rootSignatureBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_RootSignatureBlob" + ShaderInjectorIO::extensionBIN);
		}

		if (!pipeline.vsBytecode.empty()) pipelineTemplate.vertexShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalVertexShaderBytecode" + ShaderInjectorIO::extensionBIN);
		if (!pipeline.psBytecode.empty()) pipelineTemplate.pixelShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalPixelShaderBytecode" + ShaderInjectorIO::extensionBIN);
		if (!pipeline.csBytecode.empty()) pipelineTemplate.computeShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalComputeShaderBytecode" + ShaderInjectorIO::extensionBIN);
		if (!pipeline.gsBytecode.empty()) pipelineTemplate.geometryShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalGeometryShaderBytecode" + ShaderInjectorIO::extensionBIN);
		if (!pipeline.hsBytecode.empty()) pipelineTemplate.hullShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalHullShaderBytecode" + ShaderInjectorIO::extensionBIN);
		if (!pipeline.dsBytecode.empty()) pipelineTemplate.domainShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalDomainShaderBytecode" + ShaderInjectorIO::extensionBIN);
		if (!pipeline.asBytecode.empty()) pipelineTemplate.amplificationShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalAmplificationShaderBytecode" + ShaderInjectorIO::extensionBIN);
		if (!pipeline.msBytecode.empty()) pipelineTemplate.meshShaderBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, prefix + "_OriginalMeshShaderBytecode" + ShaderInjectorIO::extensionBIN);

		ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pipelineStreamBlobPath, pipeline.streamBlob.data(), pipeline.streamBlob.size()) && ok;
		ShaderTarget::ShaderPipelineStreamMetadataDisk metadata = BuildPipelineStreamMetadata(pipeline);
		ok = ShaderTarget::WritePipelineStreamMetadataJson(pipelineTemplate.pipelineStreamMetadataPath, metadata) && ok;
		if (!cachedBlob.empty() && !pipelineTemplate.pipelineCachedBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pipelineCachedBlobPath, cachedBlob.data(), cachedBlob.size()) && ok;
		if (!rootSignatureBlob.empty() && !pipelineTemplate.rootSignatureBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.rootSignatureBlobPath, rootSignatureBlob.data(), rootSignatureBlob.size()) && ok;
		if (!pipeline.vsBytecode.empty() && !pipelineTemplate.vertexShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.vertexShaderBlobPath, pipeline.vsBytecode.data(), pipeline.vsBytecode.size()) && ok;
		if (!pipeline.psBytecode.empty() && !pipelineTemplate.pixelShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pixelShaderBlobPath, pipeline.psBytecode.data(), pipeline.psBytecode.size()) && ok;
		if (!pipeline.csBytecode.empty() && !pipelineTemplate.computeShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.computeShaderBlobPath, pipeline.csBytecode.data(), pipeline.csBytecode.size()) && ok;
		if (!pipeline.gsBytecode.empty() && !pipelineTemplate.geometryShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.geometryShaderBlobPath, pipeline.gsBytecode.data(), pipeline.gsBytecode.size()) && ok;
		if (!pipeline.hsBytecode.empty() && !pipelineTemplate.hullShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.hullShaderBlobPath, pipeline.hsBytecode.data(), pipeline.hsBytecode.size()) && ok;
		if (!pipeline.dsBytecode.empty() && !pipelineTemplate.domainShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.domainShaderBlobPath, pipeline.dsBytecode.data(), pipeline.dsBytecode.size()) && ok;
		if (!pipeline.asBytecode.empty() && !pipelineTemplate.amplificationShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.amplificationShaderBlobPath, pipeline.asBytecode.data(), pipeline.asBytecode.size()) && ok;
		if (!pipeline.msBytecode.empty() && !pipelineTemplate.meshShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.meshShaderBlobPath, pipeline.msBytecode.data(), pipeline.msBytecode.size()) && ok;

		replacement.pipelineTemplates.push_back(pipelineTemplate);
		return true;
	}

	void WriteMatchingStreamPipelineTemplateVariants(ShaderTarget::ShaderTargetDisk& replacement, ShaderTarget::ShaderType shaderType, uint64_t shaderHash, bool& ok)
	{
		if (shaderHash == 0)
			return;

		int templateIndex = static_cast<int>(replacement.pipelineTemplates.size());
		int capturedTemplateCount = 0;
		for (int i = 0; i < (int)gPipelineStates.size(); ++i)
		{
			const PipelineStateInfo& pipeline = gPipelineStates[i];
			if (!StreamPipelineHasShaderHash(pipeline, shaderType, shaderHash))
				continue;

			if (WriteStreamPipelineTemplateVariant(replacement, pipeline, i, templateIndex++, ok))
				++capturedTemplateCount;
		}

		if (capturedTemplateCount > 1)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12ReplacementTemplates->WriteMatchingStreamPipelineTemplateVariants: Captured stream pipeline template variants for " + replacement.name + ": " + std::to_string(capturedTemplateCount));
	}

	bool PersistAppliedStreamPipelineTemplate(
		ShaderTarget::ShaderTargetDisk& replacement,
		const PipelineStateInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash)
	{
		if (shaderHash == 0 || !StreamPipelineHasShaderHash(pipeline, shaderType, shaderHash) || pipeline.streamBlob.empty())
			return false;

		ShaderTarget::ShaderPipelineTemplateDisk candidateTemplate{};
		FillPipelineTemplateCommonState(candidateTemplate, pipeline);

		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		if (GetPipelineCachedBlobInfo(pipeline.pipelineState, cachedBlobHash, cachedBlobSize, nullptr))
		{
			candidateTemplate.pipelineCachedBlobHash = Hash::FormatHash(cachedBlobHash);
			candidateTemplate.pipelineCachedBlobLength = std::to_string(cachedBlobSize);
		}

		for (const ShaderTarget::ShaderPipelineTemplateDisk& existingTemplate : replacement.pipelineTemplates)
		{
			const bool candidateHasCachedIdentity = !candidateTemplate.pipelineCachedBlobHash.empty();
			const bool existingHasCachedIdentity = !existingTemplate.pipelineCachedBlobHash.empty();

			if (candidateHasCachedIdentity || existingHasCachedIdentity)
			{
				// A cached PSO hash identifies the complete driver pipeline, including fixed-function
				// state that is not represented by shader/input-layout metadata. Distinct hashes must
				// remain distinct templates; otherwise light-volume cull/depth variants can collapse
				// onto one persisted stream and render incorrectly on the next application run.
				if (candidateHasCachedIdentity && existingHasCachedIdentity &&
					existingTemplate.pipelineCachedBlobHash == candidateTemplate.pipelineCachedBlobHash)
				{
					return false;
				}

				continue;
			}

			// Structural deduplication is only a fallback when neither pipeline supplied a
			// reliable cached identity.
			if (existingTemplate.pipelineStreamSubobjectTypes == candidateTemplate.pipelineStreamSubobjectTypes &&
				existingTemplate.vsHash == candidateTemplate.vsHash &&
				existingTemplate.psHash == candidateTemplate.psHash &&
				existingTemplate.csHash == candidateTemplate.csHash &&
				existingTemplate.gsHash == candidateTemplate.gsHash &&
				existingTemplate.hsHash == candidateTemplate.hsHash &&
				existingTemplate.dsHash == candidateTemplate.dsHash &&
				existingTemplate.asHash == candidateTemplate.asHash &&
				existingTemplate.msHash == candidateTemplate.msHash &&
				existingTemplate.inputLayoutSignature == candidateTemplate.inputLayoutSignature &&
				existingTemplate.streamOutputSignature == candidateTemplate.streamOutputSignature)
			{
				return false;
			}
		}

		bool ok = true;
		const int templateIndex = static_cast<int>(replacement.pipelineTemplates.size());
		if (!WriteStreamPipelineTemplateVariant(replacement, pipeline, pipelineIndex, templateIndex, ok))
			return false;

		replacement.schemaVersion = 5;
		ok = ShaderTarget::WriteShaderTargetJson(replacement) && ok;
		if (!ok)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"HookD3D12ReplacementTemplates->PersistAppliedStreamPipelineTemplate: failed for " +
				replacement.name);
			return false;
		}

		ShaderInjectorGUI::WriteToRuntimeLog(
			"HookD3D12ReplacementTemplates->PersistAppliedStreamPipelineTemplate: captured " +
			replacement.pipelineTemplates.back().name + " for " + replacement.name);
		return true;
	}

	bool PersistStreamPipelineTemplatesForShaderAlias(
		ShaderTarget::ShaderTargetDisk& replacement,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash)
	{
		if (shaderHash == 0)
			return false;

		auto templateShaderHash = [shaderType](const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate)
		{
			switch (shaderType)
			{
				case ShaderTarget::VertexShader: return Hash::ParseHashText(pipelineTemplate.vsHash);
				case ShaderTarget::HullShader: return Hash::ParseHashText(pipelineTemplate.hsHash);
				case ShaderTarget::DomainShader: return Hash::ParseHashText(pipelineTemplate.dsHash);
				case ShaderTarget::GeometryShader: return Hash::ParseHashText(pipelineTemplate.gsHash);
				case ShaderTarget::PixelShader: return Hash::ParseHashText(pipelineTemplate.psHash);
				case ShaderTarget::ComputeShader: return Hash::ParseHashText(pipelineTemplate.csHash);
				default: return uint64_t{ 0 };
			}
		};

		for (const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			if (templateShaderHash(pipelineTemplate) == shaderHash)
				return false;
		}

		const size_t previousTemplateCount = replacement.pipelineTemplates.size();
		bool writeSucceeded = true;
		WriteMatchingStreamPipelineTemplateVariants(replacement, shaderType, shaderHash, writeSucceeded);
		if (replacement.pipelineTemplates.size() == previousTemplateCount)
			return false;

		replacement.schemaVersion = 5;
		writeSucceeded = ShaderTarget::WriteShaderTargetJson(replacement) && writeSucceeded;
		if (!writeSucceeded)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"HookD3D12ReplacementTemplates->PersistStreamPipelineTemplatesForShaderAlias: failed for " + replacement.name);
			return false;
		}

		ShaderInjectorGUI::WriteToRuntimeLogSuccess(
			"HookD3D12ReplacementTemplates->PersistStreamPipelineTemplatesForShaderAlias: captured " +
			std::to_string(replacement.pipelineTemplates.size() - previousTemplateCount) +
			" current-version templates for " + replacement.name);
		return true;
	}

	bool SelectPersistedPipelineTemplateForUncaptured(
		const ShaderTarget::ShaderTargetDisk& replacement,
		const UncapturedPipelineStateInfo& uncaptured,
		ShaderTarget::ShaderTargetDisk& outTemplateReplacement,
		std::string& outTemplateName,
		SIZE_T& outMatchingBytes)
	{
		outTemplateReplacement = replacement;
		outTemplateName.clear();
		outMatchingBytes = 0;

		if (replacement.pipelineTemplates.empty())
			return true;

		int bestIndex = -1;
		SIZE_T bestMatchingBytes = 0;
		int exactHashIndex = -1;

		for (int i = 0; i < (int)replacement.pipelineTemplates.size(); ++i)
		{
			const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate = replacement.pipelineTemplates[i];
			const uint64_t templateHash = Hash::ParseHashText(pipelineTemplate.pipelineCachedBlobHash);
			if (templateHash != 0 && templateHash == uncaptured.cachedBlobHash)
			{
				exactHashIndex = i;
				break;
			}

			if (pipelineTemplate.pipelineCachedBlobLength.empty() || uncaptured.cachedBlobSize == 0)
				continue;

			const SIZE_T templateSize = (SIZE_T)_strtoui64(pipelineTemplate.pipelineCachedBlobLength.c_str(), nullptr, 10);
			if (templateSize != uncaptured.cachedBlobSize)
				continue;

			SIZE_T matchingBytes = 1;
			if (!uncaptured.cachedBlob.empty() && !pipelineTemplate.pipelineCachedBlobPath.empty())
			{
				std::vector<uint8_t> templateCachedBlob;
				if (ShaderInjectorIO::LoadDXILBlobFromDisk(pipelineTemplate.pipelineCachedBlobPath, templateCachedBlob) && templateCachedBlob.size() == uncaptured.cachedBlob.size())
					matchingBytes = CountMatchingBytes(templateCachedBlob, uncaptured.cachedBlob);
			}

			if (matchingBytes > bestMatchingBytes)
			{
				bestMatchingBytes = matchingBytes;
				bestIndex = i;
			}
		}

		const int selectedIndex = exactHashIndex >= 0 ? exactHashIndex : bestIndex;
		if (selectedIndex < 0)
			return true;

		const ShaderTarget::ShaderPipelineTemplateDisk& selectedTemplate = replacement.pipelineTemplates[selectedIndex];
		outTemplateReplacement = ReplacementWithPipelineTemplate(replacement, selectedTemplate);
		outTemplateName = selectedTemplate.name;
		outMatchingBytes = exactHashIndex >= 0 ? uncaptured.cachedBlobSize : bestMatchingBytes;
		return true;
	}
}
