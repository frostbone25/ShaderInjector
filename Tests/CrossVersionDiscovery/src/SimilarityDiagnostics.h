#pragma once

#include <string>
#include <vector>

#include "ShaderAnalysis.h"

namespace CrossVersionDiscoveryTest
{
	struct SimilarityFieldScore
	{
		std::string name;
		double score = 0.0;
		double weight = 0.0;
	};

	struct SimilarityDiagnostics
	{
		double totalScore = 0.0;
		std::vector<SimilarityFieldScore> fields;
		std::vector<std::string> lowScoringFields(double threshold = 0.99) const;
	};

	SimilarityDiagnostics AnalyzeShaderSimilarity(
		const ShaderAnalysis::ShaderAnalysisDisk& left,
		const ShaderAnalysis::ShaderAnalysisDisk& right);
}
