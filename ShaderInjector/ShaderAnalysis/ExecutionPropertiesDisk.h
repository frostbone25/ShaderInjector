#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "JsonHelper.h"

namespace ShaderAnalysis
{
	//record stage-specific execution details such as geometry and thread-group sizes.
	struct ExecutionPropertiesDisk
	{
		uint32_t geometryOutputTopology = 0;
		uint32_t geometryMaximumOutputVertexCount = 0;
		uint32_t inputPrimitive = 0;
		uint32_t geometryInstanceCount = 0;
		uint32_t controlPointCount = 0;
		uint32_t hullOutputPrimitive = 0;
		uint32_t hullPartitioning = 0;
		uint32_t tessellatorDomain = 0;
		uint32_t threadGroupSizeX = 0;
		uint32_t threadGroupSizeY = 0;
		uint32_t threadGroupSizeZ = 0;
		uint32_t threadGroupTotalSize = 0;
		uint32_t minimumFeatureLevel = 0;
		uint64_t requiresFlags = 0;
		bool sampleFrequencyShader = false;

		double CalculateSimilarityScore(const ExecutionPropertiesDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ExecutionPropertiesDisk>& left, const std::vector<ExecutionPropertiesDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ExecutionPropertiesDisk,
			geometryOutputTopology,
			geometryMaximumOutputVertexCount,
			inputPrimitive,
			geometryInstanceCount,
			controlPointCount,
			hullOutputPrimitive,
			hullPartitioning,
			tessellatorDomain,
			threadGroupSizeX,
			threadGroupSizeY,
			threadGroupSizeZ,
			threadGroupTotalSize,
			minimumFeatureLevel,
			requiresFlags,
			sampleFrequencyShader)
	};
} //namespace ShaderAnalysis
