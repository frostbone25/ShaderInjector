#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"

namespace ShaderAnalysis
{
	//describe one shader input or output parameter as reflected from bytecode.
	struct SignatureParameterDisk
	{
		std::string semanticName;
		uint32_t semanticIndex = 0;
		uint32_t registerIndex = 0;
		uint32_t systemValueType = 0;
		uint32_t componentType = 0;
		uint32_t mask = 0;
		uint32_t readWriteMask = 0;
		uint32_t stream = 0;
		uint32_t minimumPrecision = 0;

		double CalculateSimilarityScore(const SignatureParameterDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<SignatureParameterDisk>& left, const std::vector<SignatureParameterDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			SignatureParameterDisk,
			semanticName,
			semanticIndex,
			registerIndex,
			systemValueType,
			componentType,
			mask,
			readWriteMask,
			stream,
			minimumPrecision)
	};
} //namespace ShaderAnalysis
