#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "JsonHelper.h"

namespace ShaderTarget
{
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
}
