#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"

namespace ShaderAnalysis
{
	//record the reflected register range and resource kind for a shader binding.
	struct ResourceBindingDisk
	{
		std::string name;
		uint32_t type = 0;
		uint32_t bindPoint = 0;
		uint32_t bindCount = 0;
		uint32_t flags = 0;
		uint32_t returnType = 0;
		uint32_t dimension = 0;
		uint32_t sampleCountOrStride = 0;
		uint32_t registerSpace = 0;
		uint32_t rangeId = 0;

		double CalculateSimilarityScore(const ResourceBindingDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ResourceBindingDisk>& left, const std::vector<ResourceBindingDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ResourceBindingDisk,
			name,
			type,
			bindPoint,
			bindCount,
			flags,
			returnType,
			dimension,
			sampleCountOrStride,
			registerSpace,
			rangeId)
	};
} //namespace ShaderAnalysis
