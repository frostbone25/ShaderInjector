//HookD3D12ReplacementTemplates.cpp

#include <algorithm>
#include <cmath>

//custom
#include "Hash.h"
#include "HookD3D12.h"
#include "ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
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

		if (!replacement.pipelineStreamBlobPath.empty())
		{
			PipelineStateInfo persistedPipeline{};
			if (LoadPersistedStreamTemplateFromReplacement(replacement, persistedPipeline))
				FillStreamReplacementPortableStateFromBlob(replacement, persistedPipeline);
		}

		for (ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			ShaderTarget::ShaderTargetDisk templateReplacement =
				ReplacementWithPipelineTemplate(replacement, pipelineTemplate);
			PipelineStateInfo templatePipeline{};
			if (LoadPersistedStreamTemplateFromReplacement(templateReplacement, templatePipeline))
				FillPipelineTemplateCommonState(pipelineTemplate, templatePipeline);
		}
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

		ShaderTarget::ShaderTargetDisk portableState{};
		FillStreamReplacementPortableStateFromBlob(portableState, pipeline);
		pipelineTemplate.renderTargetFormat0 = portableState.renderTargetFormat0;
		pipelineTemplate.renderTargetFormats = portableState.renderTargetFormats;
		pipelineTemplate.numRenderTargets = portableState.numRenderTargets;
		pipelineTemplate.depthStencilFormat = portableState.depthStencilFormat;
		pipelineTemplate.primitiveTopologyType = portableState.primitiveTopologyType;
		pipelineTemplate.sampleCount = portableState.sampleCount;
		pipelineTemplate.sampleQuality = portableState.sampleQuality;
		pipelineTemplate.sampleMask = portableState.sampleMask;
		pipelineTemplate.blendStateHash = portableState.blendStateHash;
		pipelineTemplate.rasterizerStateHash = portableState.rasterizerStateHash;
		pipelineTemplate.depthStencilStateHash = portableState.depthStencilStateHash;
		pipelineTemplate.pipelineFixedFunctionStateHash = portableState.pipelineFixedFunctionStateHash;
	}

	ShaderTarget::ShaderTargetDisk ReplacementWithPipelineTemplate(const ShaderTarget::ShaderTargetDisk& replacement, const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate)
	{
		ShaderTarget::ShaderTargetDisk templateReplacement = replacement;
		templateReplacement.name = replacement.name + "/" + pipelineTemplate.name;
		templateReplacement.pipelineIndex = pipelineTemplate.pipelineIndex;
		templateReplacement.psoPointer = pipelineTemplate.psoPointer;
		templateReplacement.pipelineCachedBlobHash = pipelineTemplate.pipelineCachedBlobHash;
		templateReplacement.pipelineCachedBlobHashAliases = pipelineTemplate.pipelineCachedBlobHashAliases;
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
		templateReplacement.renderTargetFormat0 = pipelineTemplate.renderTargetFormat0;
		templateReplacement.renderTargetFormats = pipelineTemplate.renderTargetFormats;
		templateReplacement.numRenderTargets = pipelineTemplate.numRenderTargets;
		templateReplacement.depthStencilFormat = pipelineTemplate.depthStencilFormat;
		templateReplacement.primitiveTopologyType = pipelineTemplate.primitiveTopologyType;
		templateReplacement.sampleCount = pipelineTemplate.sampleCount;
		templateReplacement.sampleQuality = pipelineTemplate.sampleQuality;
		templateReplacement.sampleMask = pipelineTemplate.sampleMask;
		templateReplacement.blendStateHash = pipelineTemplate.blendStateHash;
		templateReplacement.rasterizerStateHash = pipelineTemplate.rasterizerStateHash;
		templateReplacement.depthStencilStateHash = pipelineTemplate.depthStencilStateHash;
		templateReplacement.pipelineFixedFunctionStateHash = pipelineTemplate.pipelineFixedFunctionStateHash;
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

	bool PipelineTemplateHasSameRebuildIdentity(
		const ShaderTarget::ShaderPipelineTemplateDisk& left,
		const ShaderTarget::ShaderPipelineTemplateDisk& right)
	{
		if (left.pipelineFixedFunctionStateHash.empty() ||
			left.pipelineFixedFunctionStateHash != right.pipelineFixedFunctionStateHash)
		{
			return false;
		}

		return left.vsHash == right.vsHash &&
			left.psHash == right.psHash &&
			left.csHash == right.csHash &&
			left.gsHash == right.gsHash &&
			left.hsHash == right.hsHash &&
			left.dsHash == right.dsHash &&
			left.asHash == right.asHash &&
			left.msHash == right.msHash &&
			left.inputLayoutSignature == right.inputLayoutSignature &&
			left.streamOutputSignature == right.streamOutputSignature;
	}

	ShaderTarget::ShaderPipelineTemplateDisk TopLevelPipelineTemplate(
		const ShaderTarget::ShaderTargetDisk& replacement)
	{
		ShaderTarget::ShaderPipelineTemplateDisk pipelineTemplate{};
		pipelineTemplate.pipelineCachedBlobHash = replacement.pipelineCachedBlobHash;
		pipelineTemplate.pipelineCachedBlobHashAliases = replacement.pipelineCachedBlobHashAliases;
		pipelineTemplate.pipelineFixedFunctionStateHash = replacement.pipelineFixedFunctionStateHash;
		pipelineTemplate.vsHash = replacement.vsHash;
		pipelineTemplate.psHash = replacement.psHash;
		pipelineTemplate.csHash = replacement.csHash;
		pipelineTemplate.gsHash = replacement.gsHash;
		pipelineTemplate.hsHash = replacement.hsHash;
		pipelineTemplate.dsHash = replacement.dsHash;
		pipelineTemplate.asHash = replacement.asHash;
		pipelineTemplate.msHash = replacement.msHash;
		pipelineTemplate.inputLayoutSignature = replacement.inputLayoutSignature;
		pipelineTemplate.streamOutputSignature = replacement.streamOutputSignature;
		return pipelineTemplate;
	}

	bool AddCachedBlobHashAlias(
		const std::string& primaryHash,
		std::vector<std::string>& hashAliases,
		uint64_t cachedBlobHash)
	{
		if (!cachedBlobHash || Hash::ParseHashText(primaryHash) == cachedBlobHash)
			return false;

		const auto existingAlias = std::find_if(
			hashAliases.begin(),
			hashAliases.end(),
			[cachedBlobHash](const std::string& hashAlias)
			{
				return Hash::ParseHashText(hashAlias) == cachedBlobHash;
			});
		if (existingAlias != hashAliases.end())
			return false;

		hashAliases.push_back(Hash::FormatHash(cachedBlobHash));
		return true;
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

			ShaderTarget::ShaderPipelineTemplateDisk candidateTemplate{};
			FillPipelineTemplateCommonState(candidateTemplate, pipeline);
			const bool alreadyCaptured =
				PipelineTemplateHasSameRebuildIdentity(
					candidateTemplate,
					TopLevelPipelineTemplate(replacement)) ||
				std::any_of(
					replacement.pipelineTemplates.begin(),
					replacement.pipelineTemplates.end(),
					[&candidateTemplate](const ShaderTarget::ShaderPipelineTemplateDisk& existingTemplate)
					{
						return PipelineTemplateHasSameRebuildIdentity(candidateTemplate, existingTemplate);
					});
			if (alreadyCaptured)
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

		auto persistLearnedCacheAlias = [&](const std::string& primaryHash, std::vector<std::string>& aliases, const std::string& variantName)
		{
			if (!AddCachedBlobHashAlias(primaryHash, aliases, cachedBlobHash))
				return;

			replacement.schemaVersion = 6;
			if (!ShaderTarget::WriteShaderTargetJson(replacement))
			{
				aliases.pop_back();
				ShaderInjectorGUI::WriteToRuntimeLogWarning(
					"HookD3D12ReplacementTemplates->PersistAppliedStreamPipelineTemplate: failed to persist cache alias for " +
					replacement.name + variantName);
				return;
			}

			ShaderInjectorGUI::WriteToRuntimeLog(
				"HookD3D12ReplacementTemplates->PersistAppliedStreamPipelineTemplate: learned cache alias " +
				Hash::FormatHash(cachedBlobHash) + " for " + replacement.name + variantName);
		};

		const ShaderTarget::ShaderPipelineTemplateDisk topLevelTemplate =
			TopLevelPipelineTemplate(replacement);
		if (PipelineTemplateHasSameRebuildIdentity(candidateTemplate, topLevelTemplate))
		{
			persistLearnedCacheAlias(
				replacement.pipelineCachedBlobHash,
				replacement.pipelineCachedBlobHashAliases,
				"");
			return false;
		}

		for (ShaderTarget::ShaderPipelineTemplateDisk& existingTemplate : replacement.pipelineTemplates)
		{
			if (!PipelineTemplateHasSameRebuildIdentity(candidateTemplate, existingTemplate))
				continue;

			persistLearnedCacheAlias(
				existingTemplate.pipelineCachedBlobHash,
				existingTemplate.pipelineCachedBlobHashAliases,
				"/" + existingTemplate.name);
			return false;
		}

		bool ok = true;
		const int templateIndex = static_cast<int>(replacement.pipelineTemplates.size());
		if (!WriteStreamPipelineTemplateVariant(replacement, pipeline, pipelineIndex, templateIndex, ok))
			return false;

		replacement.schemaVersion = 6;
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

		const size_t previousTemplateCount = replacement.pipelineTemplates.size();
		bool writeSucceeded = true;
		WriteMatchingStreamPipelineTemplateVariants(replacement, shaderType, shaderHash, writeSucceeded);
		if (replacement.pipelineTemplates.size() == previousTemplateCount)
			return false;

		replacement.schemaVersion = 6;
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

	bool PersistObservedPipelineCacheAlias(
		ShaderTarget::ShaderTargetDisk& replacement,
		const std::string& pipelineTemplateName,
		uint64_t cachedBlobHash)
	{
		std::string* primaryHash = &replacement.pipelineCachedBlobHash;
		std::vector<std::string>* aliases = &replacement.pipelineCachedBlobHashAliases;
		if (!pipelineTemplateName.empty())
		{
			auto pipelineTemplate = std::find_if(
				replacement.pipelineTemplates.begin(),
				replacement.pipelineTemplates.end(),
				[&pipelineTemplateName](const ShaderTarget::ShaderPipelineTemplateDisk& candidate)
				{
					return candidate.name == pipelineTemplateName;
				});
			if (pipelineTemplate == replacement.pipelineTemplates.end())
				return false;

			primaryHash = &pipelineTemplate->pipelineCachedBlobHash;
			aliases = &pipelineTemplate->pipelineCachedBlobHashAliases;
		}

		if (!AddCachedBlobHashAlias(*primaryHash, *aliases, cachedBlobHash))
			return false;

		replacement.schemaVersion = 6;
		if (!ShaderTarget::WriteShaderTargetJson(replacement))
		{
			aliases->pop_back();
			ShaderInjectorGUI::WriteToRuntimeLogWarning(
				"HookD3D12ReplacementTemplates->PersistObservedPipelineCacheAlias: failed to persist " +
				replacement.name);
			return false;
		}

		ShaderInjectorGUI::WriteToRuntimeLog(
			"HookD3D12ReplacementTemplates->PersistObservedPipelineCacheAlias: learned exact warm-cache identity " +
			Hash::FormatHash(cachedBlobHash) + " for " + replacement.name +
			(pipelineTemplateName.empty() ? "" : "/" + pipelineTemplateName));
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
		const bool topLevelEntryIsValid =
			PersistedPipelineEntryTargetsShader(replacement, replacement);
		const auto cacheHashMatches = [cachedBlobHash = uncaptured.cachedBlobHash](
			const std::string& primaryHash,
			const std::vector<std::string>& aliases)
		{
			if (Hash::ParseHashText(primaryHash) == cachedBlobHash)
				return true;
			return std::any_of(
				aliases.begin(),
				aliases.end(),
				[cachedBlobHash](const std::string& alias)
				{
					return Hash::ParseHashText(alias) == cachedBlobHash;
				});
		};

		// Exact cache identities select both the target and its fixed-function variant.
		if (topLevelEntryIsValid &&
			cacheHashMatches(
				replacement.pipelineCachedBlobHash,
				replacement.pipelineCachedBlobHashAliases))
		{
			outMatchingBytes = uncaptured.cachedBlobSize;
			return true;
		}

		for (int templateIndex = 0;
			templateIndex < static_cast<int>(replacement.pipelineTemplates.size());
			++templateIndex)
		{
			const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate =
				replacement.pipelineTemplates[templateIndex];
			if (!PersistedPipelineEntryTargetsShader(replacement, pipelineTemplate) ||
				!cacheHashMatches(
					pipelineTemplate.pipelineCachedBlobHash,
					pipelineTemplate.pipelineCachedBlobHashAliases))
			{
				continue;
			}

			outTemplateReplacement = ReplacementWithPipelineTemplate(replacement, pipelineTemplate);
			outTemplateName = pipelineTemplate.name;
			outMatchingBytes = uncaptured.cachedBlobSize;
			return true;
		}

		uint64_t observedRootSignatureHash = 0;
		std::vector<uint8_t> observedRootSignatureBlob;
		ID3D12RootSignature* observedRootSignature = replacement.shaderType == ShaderTarget::ComputeShader
			? uncaptured.observedComputeRootSignature
			: uncaptured.observedGraphicsRootSignature;
		GetRootSignatureBlob(
			observedRootSignature,
			observedRootSignatureBlob,
			observedRootSignatureHash);

		const auto metadataMatches = [&](const std::string& serializedLength, const std::string& serializedRootHash)
		{
			if (!observedRootSignatureHash || serializedLength.empty())
				return false;
			char* parseEnd = nullptr;
			const unsigned long long parsedLength = _strtoui64(serializedLength.c_str(), &parseEnd, 10);
			return parseEnd != serializedLength.c_str() && *parseEnd == '\0' &&
				parsedLength == uncaptured.cachedBlobSize &&
				Hash::ParseHashText(serializedRootHash) == observedRootSignatureHash;
		};

		if (!uncaptured.cachedBlob.empty())
		{
			int bestTemplateIndex = -2; // -1 is the top-level entry.
			double bestMatchingRatio = 0.0;
			bool bestMatchIsAmbiguous = false;
			const double oneByteRatio = 1.0 / static_cast<double>(uncaptured.cachedBlob.size());

			auto pipelineStreamPathForIndex = [&](int templateIndex) -> const std::string&
			{
				return templateIndex < 0
					? replacement.pipelineStreamBlobPath
					: replacement.pipelineTemplates[templateIndex].pipelineStreamBlobPath;
			};

			auto considerContentMatch = [&](int templateIndex, const std::string& blobPath, const std::string& blobLength)
			{
				double matchingRatio = 0.0;
				size_t longestMatchingRun = 0;
				if (!MatchPersistedCachedBlobContent(
					blobPath,
					blobLength,
					uncaptured.cachedBlob,
					matchingRatio,
					longestMatchingRun))
				{
					return;
				}

				if (bestTemplateIndex == -2 || matchingRatio > bestMatchingRatio + oneByteRatio)
				{
					bestTemplateIndex = templateIndex;
					bestMatchingRatio = matchingRatio;
					bestMatchIsAmbiguous = false;
					return;
				}

				if (std::abs(matchingRatio - bestMatchingRatio) <= oneByteRatio &&
					!PersistedPipelineStreamsAreEquivalent(
						pipelineStreamPathForIndex(bestTemplateIndex),
						pipelineStreamPathForIndex(templateIndex)))
				{
					bestMatchIsAmbiguous = true;
				}
			};

			if (topLevelEntryIsValid)
			{
				considerContentMatch(
					-1,
					replacement.pipelineCachedBlobPath,
					replacement.pipelineCachedBlobLength);
			}

			for (int templateIndex = 0;
				templateIndex < static_cast<int>(replacement.pipelineTemplates.size());
				++templateIndex)
			{
				const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate =
					replacement.pipelineTemplates[templateIndex];
				if (PersistedPipelineEntryTargetsShader(replacement, pipelineTemplate))
				{
					considerContentMatch(
						templateIndex,
						pipelineTemplate.pipelineCachedBlobPath,
						pipelineTemplate.pipelineCachedBlobLength);
				}
			}

			if (bestTemplateIndex != -2 && !bestMatchIsAmbiguous)
			{
				outMatchingBytes = static_cast<SIZE_T>(
					bestMatchingRatio * uncaptured.cachedBlob.size());
				if (bestTemplateIndex >= 0)
				{
					const ShaderTarget::ShaderPipelineTemplateDisk& selectedTemplate =
						replacement.pipelineTemplates[bestTemplateIndex];
					outTemplateReplacement = ReplacementWithPipelineTemplate(replacement, selectedTemplate);
					outTemplateName = selectedTemplate.name;
				}
				return true;
			}
		}

		// Metadata identifies a target only when every matching entry has the same
		// canonical fixed-function state. This recovers pointer-only stream variants
		// without guessing between genuinely different depth/raster/blend pipelines.
		int metadataTemplateIndex = -2;
		std::string metadataStreamPath;
		bool metadataIsAmbiguous = false;
		auto considerMetadataMatch = [&](int templateIndex, const std::string& length,
			const std::string& rootHash, const std::string& streamPath)
		{
			if (!metadataMatches(length, rootHash))
				return;
			if (metadataTemplateIndex == -2)
			{
				metadataTemplateIndex = templateIndex;
				metadataStreamPath = streamPath;
				return;
			}
			if (!PersistedPipelineStreamsAreEquivalent(metadataStreamPath, streamPath))
				metadataIsAmbiguous = true;
		};

		if (topLevelEntryIsValid)
		{
			considerMetadataMatch(
				-1,
				replacement.pipelineCachedBlobLength,
				replacement.rootSignatureHash,
				replacement.pipelineStreamBlobPath);
		}
		for (int templateIndex = 0;
			templateIndex < static_cast<int>(replacement.pipelineTemplates.size());
			++templateIndex)
		{
			const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate =
				replacement.pipelineTemplates[templateIndex];
			if (PersistedPipelineEntryTargetsShader(replacement, pipelineTemplate))
			{
				considerMetadataMatch(
					templateIndex,
					pipelineTemplate.pipelineCachedBlobLength,
					pipelineTemplate.rootSignatureHash,
					pipelineTemplate.pipelineStreamBlobPath);
			}
		}

		if (metadataTemplateIndex == -2 || metadataIsAmbiguous)
			return false;

		outMatchingBytes = uncaptured.cachedBlobSize;
		if (metadataTemplateIndex >= 0)
		{
			const ShaderTarget::ShaderPipelineTemplateDisk& selectedTemplate =
				replacement.pipelineTemplates[metadataTemplateIndex];
			outTemplateReplacement = ReplacementWithPipelineTemplate(replacement, selectedTemplate);
			outTemplateName = selectedTemplate.name;
		}
		return true;
	}
}
