#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h" //NLOHMANN

namespace ShaderReplacement
{
	enum ShaderType
	{
		VertexShader   = 0,
		HullShader     = 1,
		DomainShader   = 2,
		GeometryShader = 3,
		PixelShader    = 4,
		ComputeShader  = 5,
		Unknown        = 6,
	};


	struct ShaderPipelineTemplateDisk
	{
		std::string name;
		std::string sourceList;
		std::string pipelineIndex;
		std::string psoPointer;
		std::string pipelineCachedBlobHash;
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
		std::string vsHash;
		std::string psHash;
		std::string csHash;
		std::string gsHash;
		std::string hsHash;
		std::string dsHash;
		std::string vsLength;
		std::string psLength;
		std::string csLength;
		std::string gsLength;
		std::string hsLength;
		std::string dsLength;
		std::string pipelineStreamLength;
		std::string pipelineStreamSubobjectTypes;
		std::string inputLayoutElementCount;
		std::string inputLayoutSignature;
		std::string streamOutputDeclarationCount;
		std::string streamOutputSignature;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderPipelineTemplateDisk,
			name,
			sourceList,
			pipelineIndex,
			psoPointer,
			pipelineCachedBlobHash,
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
			vsHash,
			psHash,
			csHash,
			gsHash,
			hsHash,
			dsHash,
			vsLength,
			psLength,
			csLength,
			gsLength,
			hsLength,
			dsLength,
			pipelineStreamLength,
			pipelineStreamSubobjectTypes,
			inputLayoutElementCount,
			inputLayoutSignature,
			streamOutputDeclarationCount,
			streamOutputSignature)
	};

	//data structure holding information about the replacement shader serialized to the disk (JSON TEXT)
	struct ShaderReplacementDisk
	{
		int schemaVersion = 1;
		bool enabled = true;

		//name of the replacement shader
		//by default if one isn't specified it should just be ShaderReplacement_{HASH}
		std::string name;

		ShaderType shaderType = Unknown;

		//The shader profile to compile as
		std::string shaderProfile; //ps_6_6
		std::string shaderEntryPoint = "main";

		std::string originalShaderBytecodeHash;
		std::string originalShaderBytecodeLength;
		std::string originalShaderBlobPath;

		//HLSL shader source
		std::string shaderSourceName;
		std::string shaderSourcePath;

		//Compiled shader
		std::string modifiedShaderBlobPath;

		std::string replacementDirectory;
		std::string jsonPath;

		std::string sourceList;
		std::string pipelineIndex;
		std::string pipelineStateType;
		std::string psoPointer;
		std::string pipelineCachedBlobHash;
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
		std::string targetSubobjectType;

		std::string vsHash;
		std::string psHash;
		std::string csHash;
		std::string gsHash;
		std::string hsHash;
		std::string dsHash;
		std::string vsLength;
		std::string psLength;
		std::string csLength;
		std::string gsLength;
		std::string hsLength;
		std::string dsLength;

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
		std::string pipelineStreamLength;
		std::string pipelineStreamSubobjectTypes;
		std::string rootSignatureLength;
		std::string inputLayoutElementCount;
		std::string inputLayoutSignature;
		std::string streamOutputDeclarationCount;
		std::string streamOutputSignature;
		std::vector<ShaderPipelineTemplateDisk> pipelineTemplates;

		std::string notes;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderReplacementDisk,
			schemaVersion,
			enabled,
			name,
			shaderType,
			shaderProfile,
			shaderEntryPoint,
			originalShaderBytecodeHash,
			originalShaderBytecodeLength,
			originalShaderBlobPath,
			shaderSourceName,
			shaderSourcePath,
			modifiedShaderBlobPath,
			replacementDirectory,
			jsonPath,
			sourceList,
			pipelineIndex,
			pipelineStateType,
			psoPointer,
			pipelineCachedBlobHash,
			pipelineCachedBlobLength,
			pipelineCachedBlobPath,
			pipelineStreamBlobPath,
			pipelineStreamMetadataPath,
			rootSignatureBlobPath,
			rootSignatureHash,
			vertexShaderBlobPath,
			pixelShaderBlobPath,
			computeShaderBlobPath,
			geometryShaderBlobPath,
			hullShaderBlobPath,
			domainShaderBlobPath,
			targetSubobjectType,
			vsHash,
			psHash,
			csHash,
			gsHash,
			hsHash,
			dsHash,
			vsLength,
			psLength,
			csLength,
			gsLength,
			hsLength,
			dsLength,
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
			pipelineStreamLength,
			pipelineStreamSubobjectTypes,
			rootSignatureLength,
			inputLayoutElementCount,
			inputLayoutSignature,
			streamOutputDeclarationCount,
			streamOutputSignature,
			notes)
	};

	struct ShaderInputElementDisk
	{
		std::string semanticName;
		uint32_t semanticIndex = 0;
		uint32_t format = 0;
		uint32_t inputSlot = 0;
		uint32_t alignedByteOffset = 0;
		uint32_t inputSlotClass = 0;
		uint32_t instanceDataStepRate = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderInputElementDisk,
			semanticName,
			semanticIndex,
			format,
			inputSlot,
			alignedByteOffset,
			inputSlotClass,
			instanceDataStepRate)
	};

	struct ShaderStreamOutputDeclarationDisk
	{
		std::string semanticName;
		uint32_t semanticIndex = 0;
		uint32_t startComponent = 0;
		uint32_t componentCount = 0;
		uint32_t outputSlot = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderStreamOutputDeclarationDisk,
			semanticName,
			semanticIndex,
			startComponent,
			componentCount,
			outputSlot)
	};


	struct ShaderPipelineStreamMetadataDisk
	{
		std::vector<ShaderInputElementDisk> inputElements;
		std::vector<ShaderStreamOutputDeclarationDisk> streamOutputDeclarations;
		std::vector<uint32_t> streamOutputStrides;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderPipelineStreamMetadataDisk,
			inputElements,
			streamOutputDeclarations,
			streamOutputStrides)
	};

	std::string ShaderTypeToString(ShaderReplacement::ShaderType shaderType);
	std::string ShaderProfileForType(ShaderReplacement::ShaderType shaderType);

	bool WriteShaderReplacementJson(const ShaderReplacement::ShaderReplacementDisk& replacement);
	bool LoadShaderReplacementJson(const std::string& path, ShaderReplacement::ShaderReplacementDisk& outReplacement);
	bool WritePipelineStreamMetadataJson(const std::string& path, const ShaderReplacement::ShaderPipelineStreamMetadataDisk& metadata);
	bool LoadPipelineStreamMetadataJson(const std::string& path, ShaderReplacement::ShaderPipelineStreamMetadataDisk& outMetadata);
	void CollectShaderReplacementJsonFiles(const std::string& directory, std::vector<std::string>& outJsonFiles);
}
