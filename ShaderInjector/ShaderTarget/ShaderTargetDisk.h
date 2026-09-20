#pragma once

#include <string>
#include <vector>

#include "Enum/ShaderType.h"
#include "JsonHelper.h"
#include "ShaderAnalysis.h"
#include "ShaderTarget/ShaderPipelineTemplateDisk.h"

namespace ShaderTarget
{
	struct ShaderTargetDisk
	{
		int schemaVersion = 6;
		bool enabled = true;

		//name of the replacement shader
		//by default if one isn't specified it should just be ShaderTarget_{HASH}
		std::string name;

		ShaderType shaderType = Unknown;

		//The shader profile to compile as
		std::string shaderProfile; //ps_6_6
		std::string shaderEntryPoint = "main";

		std::string originalShaderBytecodeHash;
		std::vector<std::string> shaderBytecodeHashAliases;
		std::string originalShaderBytecodeLength;
		std::string originalShaderBlobPath;
		ShaderAnalysis::ShaderAnalysisDisk originalShaderAnalysis;

		// Stable ID of the ModifiedShader package that supplies replacement HLSL.
		std::string modifiedShaderId;

		std::string replacementDirectory;
		std::string jsonPath;

		// Runtime-only cache resolved from modifiedShaderId. This path is never
		// serialized and always points into the ModifiedShader package directory.
		std::string modifiedShaderBlobPath;

		std::string sourceList;
		std::string pipelineIndex;
		std::string pipelineStateType;
		std::string psoPointer;
		std::string pipelineCachedBlobHash;
		std::vector<std::string> pipelineCachedBlobHashAliases;
		std::string pipelineCachedBlobLength;
		std::string pipelineCachedBlobPath;
		std::string pipelineStreamBlobPath;
		std::string pipelineStreamMetadataPath;
		std::string rootSignatureBlobPath;
		std::string rootSignatureHash;
		std::string vertexShaderBlobPath;
		std::string pixelShaderBlobPath;
		std::string computeShaderBlobPath;
		std::string geometryShaderBlobPath;
		std::string hullShaderBlobPath;
		std::string domainShaderBlobPath;
		std::string amplificationShaderBlobPath;
		std::string meshShaderBlobPath;
		std::string targetSubobjectType;

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
		std::string rootSignatureLength;
		std::string inputLayoutElementCount;
		std::string inputLayoutSignature;
		std::string streamOutputDeclarationCount;
		std::string streamOutputSignature;
		std::vector<ShaderPipelineTemplateDisk> pipelineTemplates;

		double CalculateSimilarityScore(const ShaderTargetDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ShaderTargetDisk>& left, const std::vector<ShaderTargetDisk>& right);
	};

	void to_json(nlohmann::ordered_json& json, const ShaderTargetDisk& shaderTarget);
	void from_json(const nlohmann::ordered_json& json, ShaderTargetDisk& shaderTarget);
}
