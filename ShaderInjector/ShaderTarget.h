#pragma once

#include <cstdint>
#include <string>
#include <vector>

//3RD Party
#include "JsonHelper.h" //NLOHMANN
#include "ShaderAnalysis.h"

/*
* Shader Target:
* This is a collection of data structures that hold as much data as possible in regards to the original PSO
* These should be automatically generated based on the users created/set modified shaders.
* The purpose is that these target specific PSOs that get collected within the game through DX12 hooks on a fresh shader cache
* Then based on the loaded modified shaders, if there is matching original bytecode (or other characteristics that the modified shader is intending to replace)
* These targets will use the modified shader bytecode, and the original PSO will be rebuilt so the game uses mostly the same PSO but with our modified shader bytecode.
*/
namespace ShaderTarget
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

	//pipeline data structure dump, used for collecting as much information as possible
	//this is really important so that on second runs of the application, we can dig through the cached PSOs
	//replace the shader bytecode with our modified one, rebuild the PSO, and we should be set.
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
			pipelineStreamLength,
			pipelineStreamSubobjectTypes,
			inputLayoutElementCount,
			inputLayoutSignature,
			streamOutputDeclarationCount,
			streamOutputSignature)
	};

	//data structure holding information about the replacement shader serialized to the disk (JSON TEXT)
	struct ShaderTargetDisk
	{
		int schemaVersion = 5;
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

	struct ShaderInputElementDisk
	{
		std::string semanticName;
		uint32_t semanticIndex = 0;
		uint32_t format = 0;
		uint32_t inputSlot = 0;
		uint32_t alignedByteOffset = 0;
		uint32_t inputSlotClass = 0;
		uint32_t instanceDataStepRate = 0;

		double CalculateSimilarityScore(const ShaderInputElementDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ShaderInputElementDisk>& left, const std::vector<ShaderInputElementDisk>& right);

		//JSON support
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

		double CalculateSimilarityScore(const ShaderStreamOutputDeclarationDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ShaderStreamOutputDeclarationDisk>& left, const std::vector<ShaderStreamOutputDeclarationDisk>& right);

		//JSON support
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
		bool hasViewInstancing = false;
		uint32_t viewInstancingFlags = 0;
		std::vector<uint32_t> viewInstanceViewportArrayIndices;
		std::vector<uint32_t> viewInstanceRenderTargetArrayIndices;

		double CalculateSimilarityScore(const ShaderPipelineStreamMetadataDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ShaderPipelineStreamMetadataDisk>& left, const std::vector<ShaderPipelineStreamMetadataDisk>& right);

		//JSON support
		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderPipelineStreamMetadataDisk,
			inputElements,
			streamOutputDeclarations,
			streamOutputStrides,
			hasViewInstancing,
			viewInstancingFlags,
			viewInstanceViewportArrayIndices,
			viewInstanceRenderTargetArrayIndices)
	};

	bool WriteShaderTargetJson(const ShaderTarget::ShaderTargetDisk& replacement);
	bool LoadShaderTargetJson(const std::string& path, ShaderTarget::ShaderTargetDisk& outReplacement);
	bool WritePipelineStreamMetadataJson(const std::string& path, const ShaderTarget::ShaderPipelineStreamMetadataDisk& metadata);
	bool LoadPipelineStreamMetadataJson(const std::string& path, ShaderTarget::ShaderPipelineStreamMetadataDisk& outMetadata);
	void CollectShaderTargetJsonFiles(const std::string& directory, std::vector<std::string>& outJsonFiles);
}
