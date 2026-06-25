#include "HookD3D12ReplacementTemplates.h"

#include <algorithm>

#include "Hash.h"
#include "HookD3D12PipelineUtils.h"
#include "HookD3D12ReplacementLookup.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"

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

	void BackfillReplacementPortableMetadataFromSidecars(ShaderReplacement::ShaderReplacementDisk& replacement)
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

	bool LoadPersistedStreamTemplateFromReplacement(const ShaderReplacement::ShaderReplacementDisk& replacement, PipelineStateInfo& outPipeline)
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
			!LoadPersistedShaderBlob(replacement.domainShaderBlobPath, outPipeline.dsBytecode, outPipeline.dsHash, outPipeline.dsSize))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementTemplates->LoadPersistedStreamTemplateFromReplacement: missing original shader stage blob for " + replacement.name);
			return false;
		}

		outPipeline.isCompute = !outPipeline.csBytecode.empty();
		outPipeline.isGraphics = !outPipeline.vsBytecode.empty() || !outPipeline.psBytecode.empty() || !outPipeline.gsBytecode.empty() || !outPipeline.hsBytecode.empty() || !outPipeline.dsBytecode.empty();

		if (!replacement.pipelineStreamMetadataPath.empty())
		{
			ShaderReplacement::ShaderPipelineStreamMetadataDisk metadata{};

			if (!ShaderReplacement::LoadPipelineStreamMetadataJson(replacement.pipelineStreamMetadataPath, metadata))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12ReplacementTemplates->LoadPersistedStreamTemplateFromReplacement: missing stream metadata for " + replacement.name);
				return false;
			}

			ApplyPipelineStreamMetadata(metadata, outPipeline);
		}

		return true;
	}

	uint64_t StreamShaderHashForType(const PipelineStateInfo& pipeline, ShaderReplacement::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderReplacement::VertexShader: return pipeline.vsHash;
			case ShaderReplacement::HullShader: return pipeline.hsHash;
			case ShaderReplacement::DomainShader: return pipeline.dsHash;
			case ShaderReplacement::GeometryShader: return pipeline.gsHash;
			case ShaderReplacement::PixelShader: return pipeline.psHash;
			case ShaderReplacement::ComputeShader: return pipeline.csHash;
			default: return 0;
		}
	}

	bool StreamPipelineHasShaderHash(const PipelineStateInfo& pipeline, ShaderReplacement::ShaderType shaderType, uint64_t shaderHash)
	{
		return shaderHash != 0 && StreamShaderHashForType(pipeline, shaderType) == shaderHash;
	}

	void FillPipelineTemplateCommonState(ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate, const PipelineStateInfo& pipeline)
	{
		pipelineTemplate.vsHash = pipeline.vsHash ? Hash::FormatHash(pipeline.vsHash) : "";
		pipelineTemplate.psHash = pipeline.psHash ? Hash::FormatHash(pipeline.psHash) : "";
		pipelineTemplate.csHash = pipeline.csHash ? Hash::FormatHash(pipeline.csHash) : "";
		pipelineTemplate.gsHash = pipeline.gsHash ? Hash::FormatHash(pipeline.gsHash) : "";
		pipelineTemplate.hsHash = pipeline.hsHash ? Hash::FormatHash(pipeline.hsHash) : "";
		pipelineTemplate.dsHash = pipeline.dsHash ? Hash::FormatHash(pipeline.dsHash) : "";
		pipelineTemplate.vsLength = pipeline.vsSize ? std::to_string((size_t)pipeline.vsSize) : "";
		pipelineTemplate.psLength = pipeline.psSize ? std::to_string((size_t)pipeline.psSize) : "";
		pipelineTemplate.csLength = pipeline.csSize ? std::to_string((size_t)pipeline.csSize) : "";
		pipelineTemplate.gsLength = pipeline.gsSize ? std::to_string((size_t)pipeline.gsSize) : "";
		pipelineTemplate.hsLength = pipeline.hsSize ? std::to_string((size_t)pipeline.hsSize) : "";
		pipelineTemplate.dsLength = pipeline.dsSize ? std::to_string((size_t)pipeline.dsSize) : "";
		pipelineTemplate.inputLayoutElementCount = std::to_string(pipeline.inputElements.size());
		pipelineTemplate.inputLayoutSignature = InputLayoutSignature(pipeline.inputElements);
		pipelineTemplate.streamOutputDeclarationCount = std::to_string(pipeline.soDeclarations.size());
		pipelineTemplate.streamOutputSignature = StreamOutputSignature(pipeline.soDeclarations, pipeline.soStrides);
		pipelineTemplate.pipelineStreamLength = pipeline.streamBlob.empty() ? "" : std::to_string(pipeline.streamBlob.size());
		pipelineTemplate.pipelineStreamSubobjectTypes = PipelineStreamSubobjectTypeSignature(pipeline.streamBlob);
	}

	ShaderReplacement::ShaderReplacementDisk ReplacementWithPipelineTemplate(const ShaderReplacement::ShaderReplacementDisk& replacement, const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate)
	{
		ShaderReplacement::ShaderReplacementDisk templateReplacement = replacement;
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
		templateReplacement.vsHash = pipelineTemplate.vsHash;
		templateReplacement.psHash = pipelineTemplate.psHash;
		templateReplacement.csHash = pipelineTemplate.csHash;
		templateReplacement.gsHash = pipelineTemplate.gsHash;
		templateReplacement.hsHash = pipelineTemplate.hsHash;
		templateReplacement.dsHash = pipelineTemplate.dsHash;
		templateReplacement.vsLength = pipelineTemplate.vsLength;
		templateReplacement.psLength = pipelineTemplate.psLength;
		templateReplacement.csLength = pipelineTemplate.csLength;
		templateReplacement.gsLength = pipelineTemplate.gsLength;
		templateReplacement.hsLength = pipelineTemplate.hsLength;
		templateReplacement.dsLength = pipelineTemplate.dsLength;
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

	bool WriteStreamPipelineTemplateVariant(ShaderReplacement::ShaderReplacementDisk& replacement, const PipelineStateInfo& pipeline, int pipelineIndex, int templateIndex, bool& ok)
	{
		if (pipeline.streamBlob.empty())
			return false;

		char prefix[64]{};
		sprintf_s(prefix, "PipelineTemplate_%03d", templateIndex);

		ShaderReplacement::ShaderPipelineTemplateDisk pipelineTemplate{};
		pipelineTemplate.name = prefix;
		pipelineTemplate.sourceList = "Stream";
		pipelineTemplate.pipelineIndex = std::to_string(pipelineIndex);
		pipelineTemplate.psoPointer = PointerToString(pipeline.pipelineState);
		pipelineTemplate.pipelineStreamBlobPath = replacement.replacementDirectory + "\\" + prefix + "_PipelineStateStream" + ShaderInjectorIO::extensionBIN;
		pipelineTemplate.pipelineStreamMetadataPath = replacement.replacementDirectory + "\\" + prefix + "_PipelineStateStreamMetadata" + ShaderInjectorIO::extensionJSON;
		FillPipelineTemplateCommonState(pipelineTemplate, pipeline);

		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;
		if (GetPipelineCachedBlobInfo(pipeline.pipelineState, cachedBlobHash, cachedBlobSize, &cachedBlob))
		{
			pipelineTemplate.pipelineCachedBlobHash = Hash::FormatHash(cachedBlobHash);
			pipelineTemplate.pipelineCachedBlobLength = std::to_string(cachedBlobSize);
			pipelineTemplate.pipelineCachedBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalPipelineCachedBlob" + ShaderInjectorIO::extensionBIN;
		}

		std::vector<uint8_t> rootSignatureBlob;
		uint64_t rootSignatureHash = 0;
		if (GetRootSignatureBlob(pipeline.rootSignature, rootSignatureBlob, rootSignatureHash))
		{
			pipelineTemplate.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
			pipelineTemplate.rootSignatureLength = std::to_string(rootSignatureBlob.size());
			pipelineTemplate.rootSignatureBlobPath = replacement.replacementDirectory + "\\" + prefix + "_RootSignatureBlob" + ShaderInjectorIO::extensionBIN;
		}

		if (!pipeline.vsBytecode.empty()) pipelineTemplate.vertexShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalVertexShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.psBytecode.empty()) pipelineTemplate.pixelShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalPixelShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.csBytecode.empty()) pipelineTemplate.computeShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalComputeShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.gsBytecode.empty()) pipelineTemplate.geometryShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalGeometryShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.hsBytecode.empty()) pipelineTemplate.hullShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalHullShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.dsBytecode.empty()) pipelineTemplate.domainShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalDomainShaderBytecode" + ShaderInjectorIO::extensionBIN;

		ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pipelineStreamBlobPath, pipeline.streamBlob.data(), pipeline.streamBlob.size()) && ok;
		ShaderReplacement::ShaderPipelineStreamMetadataDisk metadata = BuildPipelineStreamMetadata(pipeline);
		ok = ShaderReplacement::WritePipelineStreamMetadataJson(pipelineTemplate.pipelineStreamMetadataPath, metadata) && ok;
		if (!cachedBlob.empty() && !pipelineTemplate.pipelineCachedBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pipelineCachedBlobPath, cachedBlob.data(), cachedBlob.size()) && ok;
		if (!rootSignatureBlob.empty() && !pipelineTemplate.rootSignatureBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.rootSignatureBlobPath, rootSignatureBlob.data(), rootSignatureBlob.size()) && ok;
		if (!pipeline.vsBytecode.empty() && !pipelineTemplate.vertexShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.vertexShaderBlobPath, pipeline.vsBytecode.data(), pipeline.vsBytecode.size()) && ok;
		if (!pipeline.psBytecode.empty() && !pipelineTemplate.pixelShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pixelShaderBlobPath, pipeline.psBytecode.data(), pipeline.psBytecode.size()) && ok;
		if (!pipeline.csBytecode.empty() && !pipelineTemplate.computeShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.computeShaderBlobPath, pipeline.csBytecode.data(), pipeline.csBytecode.size()) && ok;
		if (!pipeline.gsBytecode.empty() && !pipelineTemplate.geometryShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.geometryShaderBlobPath, pipeline.gsBytecode.data(), pipeline.gsBytecode.size()) && ok;
		if (!pipeline.hsBytecode.empty() && !pipelineTemplate.hullShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.hullShaderBlobPath, pipeline.hsBytecode.data(), pipeline.hsBytecode.size()) && ok;
		if (!pipeline.dsBytecode.empty() && !pipelineTemplate.domainShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.domainShaderBlobPath, pipeline.dsBytecode.data(), pipeline.dsBytecode.size()) && ok;

		replacement.pipelineTemplates.push_back(pipelineTemplate);
		return true;
	}

	void WriteMatchingStreamPipelineTemplateVariants(ShaderReplacement::ShaderReplacementDisk& replacement, ShaderReplacement::ShaderType shaderType, uint64_t shaderHash, bool& ok)
	{
		if (shaderHash == 0)
			return;

		int templateIndex = 0;
		for (int i = 0; i < (int)gPipelineStates.size(); ++i)
		{
			const PipelineStateInfo& pipeline = gPipelineStates[i];
			if (!StreamPipelineHasShaderHash(pipeline, shaderType, shaderHash))
				continue;

			WriteStreamPipelineTemplateVariant(replacement, pipeline, i, templateIndex++, ok);
		}

		if (templateIndex > 1)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12ReplacementTemplates->WriteMatchingStreamPipelineTemplateVariants: Captured stream pipeline template variants for " + replacement.name + ": " + std::to_string(templateIndex));
	}

	bool SelectPersistedPipelineTemplateForUncaptured(
		const ShaderReplacement::ShaderReplacementDisk& replacement,
		const UncapturedPipelineStateInfo& uncaptured,
		ShaderReplacement::ShaderReplacementDisk& outTemplateReplacement,
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
			const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate = replacement.pipelineTemplates[i];
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

		const ShaderReplacement::ShaderPipelineTemplateDisk& selectedTemplate = replacement.pipelineTemplates[selectedIndex];
		outTemplateReplacement = ReplacementWithPipelineTemplate(replacement, selectedTemplate);
		outTemplateName = selectedTemplate.name;
		outMatchingBytes = exactHashIndex >= 0 ? uncaptured.cachedBlobSize : bestMatchingBytes;
		return true;
	}
}
