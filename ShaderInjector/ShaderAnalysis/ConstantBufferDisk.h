#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"
#include "ShaderAnalysis/ConstantBufferVariableDisk.h"

namespace ShaderAnalysis
{
	//group reflected variables under their constant buffer layout.
	struct ConstantBufferDisk
	{
		std::string name;
		uint32_t type = 0;
		uint32_t size = 0;
		uint32_t flags = 0;
		std::vector<ConstantBufferVariableDisk> variables;

		double CalculateSimilarityScore(const ConstantBufferDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ConstantBufferDisk>& left, const std::vector<ConstantBufferDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ConstantBufferDisk, name, type, size, flags, variables)
	};
} //namespace ShaderAnalysis
