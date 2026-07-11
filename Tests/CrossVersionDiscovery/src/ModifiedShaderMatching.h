#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ShaderAnalysis.h"

namespace ModifiedShader
{
	struct TargetDisk;
	struct PackageDisk;
}

namespace ModifiedShaderMatching
{
	// Mirrors ModifiedShader.cpp TargetDisk::MatchesShader (lines 46-58).
	bool TargetMatchesShader(
		const ModifiedShader::TargetDisk& target,
		uint64_t shaderHash,
		const ShaderAnalysis::ShaderAnalysisDisk& analysis);

	// Mirrors ModifiedShader.cpp TargetDisk::CalculateSimilarityScore (lines 60-70).
	double CalculateTargetSimilarityScore(const ModifiedShader::TargetDisk& left, const ModifiedShader::TargetDisk& right);

	// Mirrors ModifiedShader.cpp PackageDisk::MatchesShader (lines 77-88).
	bool PackageMatchesShader(
		const ModifiedShader::PackageDisk& package,
		uint64_t shaderHash,
		const ShaderAnalysis::ShaderAnalysisDisk& analysis);
}
