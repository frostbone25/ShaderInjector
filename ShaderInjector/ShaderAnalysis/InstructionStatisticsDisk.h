#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"

namespace ShaderAnalysis
{
	//count instruction categories so similar shaders can be compared cheaply.
	struct InstructionStatisticsDisk
	{
		uint32_t instructionCount = 0;
		uint32_t temporaryRegisterCount = 0;
		uint32_t temporaryArrayCount = 0;
		uint32_t constantDefinitionCount = 0;
		uint32_t declarationCount = 0;
		uint32_t textureNormalInstructionCount = 0;
		uint32_t textureLoadInstructionCount = 0;
		uint32_t textureComparisonInstructionCount = 0;
		uint32_t textureBiasInstructionCount = 0;
		uint32_t textureGradientInstructionCount = 0;
		uint32_t floatInstructionCount = 0;
		uint32_t signedIntegerInstructionCount = 0;
		uint32_t unsignedIntegerInstructionCount = 0;
		uint32_t staticFlowControlCount = 0;
		uint32_t dynamicFlowControlCount = 0;
		uint32_t macroInstructionCount = 0;
		uint32_t arrayInstructionCount = 0;
		uint32_t cutInstructionCount = 0;
		uint32_t emitInstructionCount = 0;
		uint32_t barrierInstructionCount = 0;
		uint32_t interlockedInstructionCount = 0;
		uint32_t textureStoreInstructionCount = 0;

		double CalculateSimilarityScore(const InstructionStatisticsDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<InstructionStatisticsDisk>& left, const std::vector<InstructionStatisticsDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			InstructionStatisticsDisk,
			instructionCount,
			temporaryRegisterCount,
			temporaryArrayCount,
			constantDefinitionCount,
			declarationCount,
			textureNormalInstructionCount,
			textureLoadInstructionCount,
			textureComparisonInstructionCount,
			textureBiasInstructionCount,
			textureGradientInstructionCount,
			floatInstructionCount,
			signedIntegerInstructionCount,
			unsignedIntegerInstructionCount,
			staticFlowControlCount,
			dynamicFlowControlCount,
			macroInstructionCount,
			arrayInstructionCount,
			cutInstructionCount,
			emitInstructionCount,
			barrierInstructionCount,
			interlockedInstructionCount,
			textureStoreInstructionCount)
	};
} //namespace ShaderAnalysis
