#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"

namespace ShaderAnalysis
{
	//describe the reflected layout of a type, including its nested members.
	struct TypeLayoutDisk
	{
		std::string name;
		uint32_t variableClass = 0;
		uint32_t variableType = 0;
		uint32_t rows = 0;
		uint32_t columns = 0;
		uint32_t elements = 0;
		uint32_t offset = 0;
		std::vector<TypeLayoutDisk> members;

		double CalculateSimilarityScore(const TypeLayoutDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<TypeLayoutDisk>& left, const std::vector<TypeLayoutDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			TypeLayoutDisk,
			name,
			variableClass,
			variableType,
			rows,
			columns,
			elements,
			offset,
			members)
	};
} //namespace ShaderAnalysis
