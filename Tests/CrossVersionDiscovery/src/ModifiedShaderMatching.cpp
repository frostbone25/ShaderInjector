#include "ModifiedShaderMatching.h"

#include <algorithm>
#include <unordered_set>

#include "Hash.h"
#include "ModifiedShader.h"
#include "SimilarityScore.h"

namespace ModifiedShaderMatching
{
	namespace
	{
		double HashCollectionSimilarity(const std::vector<std::string>& left, const std::vector<std::string>& right)
		{
			if (left.empty() && right.empty())
				return 1.0;

			const std::unordered_set<std::string> leftHashes(left.begin(), left.end());
			const std::unordered_set<std::string> rightHashes(right.begin(), right.end());
			std::unordered_set<std::string> combinedHashes = leftHashes;
			combinedHashes.insert(rightHashes.begin(), rightHashes.end());
			size_t sharedHashCount = 0;
			for (const std::string& hash : rightHashes)
			{
				if (leftHashes.find(hash) != leftHashes.end())
					++sharedHashCount;
			}

			return combinedHashes.empty()
				? 1.0
				: static_cast<double>(sharedHashCount) / static_cast<double>(combinedHashes.size());
		}

		bool AnalysesHaveSameStrictIdentity(
			const ShaderAnalysis::ShaderAnalysisDisk& left,
			const ShaderAnalysis::ShaderAnalysisDisk& right)
		{
			return left.succeeded && right.succeeded &&
				!left.crossVersionIdentityHash.empty() &&
				left.crossVersionIdentityHash == right.crossVersionIdentityHash;
		}
	}

	bool TargetMatchesShader(
		const ModifiedShader::TargetDisk& target,
		uint64_t shaderHash,
		const ShaderAnalysis::ShaderAnalysisDisk& analysis)
	{
		if (shaderHash != 0)
		{
			for (const std::string& knownHash : target.knownShaderBytecodeHashes)
			{
				if (Hash::ParseHashText(knownHash) == shaderHash)
					return true;
			}
		}

		return AnalysesHaveSameStrictIdentity(target.shaderAnalysis, analysis);
	}

	double CalculateTargetSimilarityScore(const ModifiedShader::TargetDisk& left, const ModifiedShader::TargetDisk& right)
	{
		SimilarityScore::WeightedAverage score;
		score.Add(HashCollectionSimilarity(left.knownShaderBytecodeHashes, right.knownShaderBytecodeHashes), 4.0);
		score.Add(SimilarityScore::NumericString(left.originalShaderBytecodeLength, right.originalShaderBytecodeLength), 2.0);
		if (left.shaderAnalysis.succeeded || right.shaderAnalysis.succeeded)
			score.Add(left.shaderAnalysis.CalculateSimilarityScore(right.shaderAnalysis), 10.0);
		score.Add(SimilarityScore::Exact(left.targetApplication, right.targetApplication), 1.0);
		score.Add(SimilarityScore::Exact(left.gameVersion, right.gameVersion), 0.5);
		return score.Result();
	}

	bool PackageMatchesShader(
		const ModifiedShader::PackageDisk& package,
		uint64_t shaderHash,
		const ShaderAnalysis::ShaderAnalysisDisk& analysis)
	{
		if (!package.enabled)
			return false;

		for (const ModifiedShader::TargetDisk& target : package.targets)
		{
			if (TargetMatchesShader(target, shaderHash, analysis))
				return true;
		}
		return false;
	}
}
