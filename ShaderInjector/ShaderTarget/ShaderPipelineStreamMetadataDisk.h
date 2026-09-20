#pragma once

#include <cstdint>
#include <vector>

#include "JsonHelper.h"
#include "ShaderTarget/ShaderInputElementDisk.h"
#include "ShaderTarget/ShaderStreamOutputDeclarationDisk.h"

namespace ShaderTarget
{
	struct ShaderPipelineStreamMetadataDisk
	{
		std::vector<ShaderInputElementDisk> inputElements;
		std::vector<ShaderStreamOutputDeclarationDisk> streamOutputDeclarations;
		std::vector<uint32_t> streamOutputStrides;
		bool hasViewInstancing = false;
		uint32_t viewInstancingFlags = 0;
		std::vector<uint32_t> viewInstanceViewportArrayIndices;
		std::vector<uint32_t> viewInstanceRenderTargetArrayIndices;

		double CalculateSimilarityScore(const ShaderPipelineStreamMetadataDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ShaderPipelineStreamMetadataDisk>& left, const std::vector<ShaderPipelineStreamMetadataDisk>& right);

		//JSON support
		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderPipelineStreamMetadataDisk,
			inputElements,
			streamOutputDeclarations,
			streamOutputStrides,
			hasViewInstancing,
			viewInstancingFlags,
			viewInstanceViewportArrayIndices,
			viewInstanceRenderTargetArrayIndices)
	};
}
