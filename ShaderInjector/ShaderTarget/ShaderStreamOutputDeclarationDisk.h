#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "JsonHelper.h"

namespace ShaderTarget
{
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
} //namespace ShaderTarget
