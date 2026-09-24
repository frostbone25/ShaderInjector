#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"

namespace ShaderAnalysis
{
	//record one DXBC or DXIL container part for later similarity comparisons.
	struct ContainerPartDisk
	{
		std::string fourCC;
		uint32_t kind = 0;
		uint64_t size = 0;
		std::string contentHash;

		double CalculateSimilarityScore(const ContainerPartDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ContainerPartDisk>& left, const std::vector<ContainerPartDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ContainerPartDisk, fourCC, kind, size, contentHash)
	};
} //namespace ShaderAnalysis
