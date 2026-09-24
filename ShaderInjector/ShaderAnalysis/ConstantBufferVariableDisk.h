#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"
#include "ShaderAnalysis/TypeLayoutDisk.h"

namespace ShaderAnalysis
{
	//record a variable offset, size, and type inside a constant buffer.
	struct ConstantBufferVariableDisk
	{
		std::string name;
		uint32_t startOffset = 0;
		uint32_t size = 0;
		uint32_t flags = 0;
		uint32_t startTexture = 0;
		uint32_t textureCount = 0;
		uint32_t startSampler = 0;
		uint32_t samplerCount = 0;
		TypeLayoutDisk typeLayout;

		double CalculateSimilarityScore(const ConstantBufferVariableDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ConstantBufferVariableDisk>& left, const std::vector<ConstantBufferVariableDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ConstantBufferVariableDisk,
			name,
			startOffset,
			size,
			flags,
			startTexture,
			textureCount,
			startSampler,
			samplerCount,
			typeLayout)
	};
} //namespace ShaderAnalysis
