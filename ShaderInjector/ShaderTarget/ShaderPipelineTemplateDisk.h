#pragma once

#include <string>
#include <vector>

#include "JsonHelper.h"

namespace ShaderTarget
{
	struct ShaderPipelineTemplateDisk
	{
		std::string name;
		std::string sourceList;
		std::string pipelineIndex;
		std::string psoPointer;
		std::string pipelineCachedBlobHash;
		std::vector<std::string> pipelineCachedBlobHashAliases;
		std::string pipelineCachedBlobLength;
		std::string pipelineCachedBlobPath;
		std::string pipelineStreamBlobPath;
		std::string pipelineStreamMetadataPath;
		std::string rootSignatureBlobPath;
		std::string rootSignatureHash;
		std::string rootSignatureLength;
		std::string vertexShaderBlobPath;
		std::string pixelShaderBlobPath;
		std::string computeShaderBlobPath;
		std::string geometryShaderBlobPath;
		std::string hullShaderBlobPath;
		std::string domainShaderBlobPath;
		std::string amplificationShaderBlobPath;
		std::string meshShaderBlobPath;
		std::string vsHash;
		std::string psHash;
		std::string csHash;
		std::string gsHash;
		std::string hsHash;
		std::string dsHash;
		std::string asHash;
		std::string msHash;
		std::string vsLength;
		std::string psLength;
		std::string csLength;
		std::string gsLength;
		std::string hsLength;
		std::string dsLength;
		std::string asLength;
		std::string msLength;
		std::string renderTargetFormat0;
		std::string renderTargetFormats;
		std::string numRenderTargets;
		std::string depthStencilFormat;
		std::string primitiveTopologyType;
		std::string sampleCount;
		std::string sampleQuality;
		std::string sampleMask;
		std::string blendStateHash;
		std::string rasterizerStateHash;
		std::string depthStencilStateHash;
		std::string pipelineFixedFunctionStateHash;
		std::string pipelineStreamLength;
		std::string pipelineStreamSubobjectTypes;
		std::string inputLayoutElementCount;
		std::string inputLayoutSignature;
		std::string streamOutputDeclarationCount;
		std::string streamOutputSignature;

		double CalculateSimilarityScore(const ShaderPipelineTemplateDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ShaderPipelineTemplateDisk>& left, const std::vector<ShaderPipelineTemplateDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderPipelineTemplateDisk,
			name,
			sourceList,
			pipelineIndex,
			psoPointer,
			pipelineCachedBlobHash,
			pipelineCachedBlobHashAliases,
			pipelineCachedBlobLength,
			pipelineCachedBlobPath,
			pipelineStreamBlobPath,
			pipelineStreamMetadataPath,
			rootSignatureBlobPath,
			rootSignatureHash,
			rootSignatureLength,
			vertexShaderBlobPath,
			pixelShaderBlobPath,
			computeShaderBlobPath,
			geometryShaderBlobPath,
			hullShaderBlobPath,
			domainShaderBlobPath,
			amplificationShaderBlobPath,
			meshShaderBlobPath,
			vsHash,
			psHash,
			csHash,
			gsHash,
			hsHash,
			dsHash,
			asHash,
			msHash,
			vsLength,
			psLength,
			csLength,
			gsLength,
			hsLength,
			dsLength,
			asLength,
			msLength,
			renderTargetFormat0,
			renderTargetFormats,
			numRenderTargets,
			depthStencilFormat,
			primitiveTopologyType,
			sampleCount,
			sampleQuality,
			sampleMask,
			blendStateHash,
			rasterizerStateHash,
			depthStencilStateHash,
			pipelineFixedFunctionStateHash,
			pipelineStreamLength,
			pipelineStreamSubobjectTypes,
			inputLayoutElementCount,
			inputLayoutSignature,
			streamOutputDeclarationCount,
			streamOutputSignature)
	};
}
