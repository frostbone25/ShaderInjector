#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"
#include "ShaderAnalysis/ContainerPartDisk.h"
#include "ShaderAnalysis/SignatureParameterDisk.h"
#include "ShaderAnalysis/ResourceBindingDisk.h"
#include "ShaderAnalysis/ConstantBufferDisk.h"
#include "ShaderAnalysis/InstructionStatisticsDisk.h"
#include "ShaderAnalysis/ExecutionPropertiesDisk.h"

namespace ShaderAnalysis
{
	//store the portable analysis summary used for discovery and similarity matching.
	struct ShaderAnalysisDisk
	{
		uint32_t analysisVersion = 1;
		bool succeeded = false;
		std::string error;
		std::string creator;
		uint32_t shaderVersionToken = 0;
		uint32_t shaderStage = 0;
		std::string shaderStageName;
		uint32_t shaderModelMajor = 0;
		uint32_t shaderModelMinor = 0;
		std::string shaderModelProfile;
		uint32_t compilationFlags = 0;
		std::string containerSignatureHash;
		std::string interfaceSignatureHash;
		std::string resourceSignatureHash;
		std::string constantBufferSignatureHash;
		std::string instructionStatisticsHash;
		std::string executionSignatureHash;
		std::string portableReflectionIdentityHash;
		std::string entryFunctionName;
		uint32_t normalizedInstructionCount = 0;
		uint32_t uniqueNormalizedInstructionCount = 0;
		std::string semanticInstructionSetHash;
		std::string crossVersionIdentityHash;
		std::string reflectionSignatureHash;
		std::vector<ContainerPartDisk> containerParts;
		std::vector<SignatureParameterDisk> inputParameters;
		std::vector<SignatureParameterDisk> outputParameters;
		std::vector<SignatureParameterDisk> patchConstantParameters;
		std::vector<ResourceBindingDisk> resourceBindings;
		std::vector<ConstantBufferDisk> constantBuffers;
		InstructionStatisticsDisk instructionStatistics;
		ExecutionPropertiesDisk executionProperties;

		double CalculateSimilarityScore(const ShaderAnalysisDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ShaderAnalysisDisk>& left, const std::vector<ShaderAnalysisDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderAnalysisDisk,
			analysisVersion,
			succeeded,
			error,
			creator,
			shaderVersionToken,
			shaderStage,
			shaderStageName,
			shaderModelMajor,
			shaderModelMinor,
			shaderModelProfile,
			compilationFlags,
			containerSignatureHash,
			interfaceSignatureHash,
			resourceSignatureHash,
			constantBufferSignatureHash,
			instructionStatisticsHash,
			executionSignatureHash,
			portableReflectionIdentityHash,
			entryFunctionName,
			normalizedInstructionCount,
			uniqueNormalizedInstructionCount,
			semanticInstructionSetHash,
			crossVersionIdentityHash,
			reflectionSignatureHash,
			containerParts,
			inputParameters,
			outputParameters,
			patchConstantParameters,
			resourceBindings,
			constantBuffers,
			instructionStatistics,
			executionProperties)
	};
} //namespace ShaderAnalysis
