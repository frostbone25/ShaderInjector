#include "ModifiedShader.h"

#include <unordered_set>

#include "SimilarityScore.h"

namespace ModifiedShader
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

		return combinedHashes.empty() ? 1.0 : static_cast<double>(sharedHashCount) / static_cast<double>(combinedHashes.size());
	}

	double ModifiedShaderTargetDisk::CalculateSimilarityScore(const ModifiedShaderTargetDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(HashCollectionSimilarity(knownShaderBytecodeHashes, other.knownShaderBytecodeHashes), 4.0);
		score.Add(SimilarityScore::NumericString(originalShaderBytecodeLength, other.originalShaderBytecodeLength), 2.0);

		if (shaderAnalysis.succeeded || other.shaderAnalysis.succeeded)
			score.Add(shaderAnalysis.CalculateSimilarityScore(other.shaderAnalysis), 10.0);

		score.Add(SimilarityScore::Exact(targetApplication, other.targetApplication), 1.0);
		score.Add(SimilarityScore::Exact(gameVersion, other.gameVersion), 0.5);
		return score.Result();
	}

	double ModifiedShaderTargetDisk::CalculateSimilarityScore(const std::vector<ModifiedShaderTargetDisk>& left, const std::vector<ModifiedShaderTargetDisk>& right)
	{
		return SimilarityScore::CalculateCollectionSimilarityScore(left, right);
	}

	double ModifiedShaderPackageDisk::CalculateSimilarityScore(const ModifiedShaderPackageDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(id, other.id), 2.0);
		score.Add(SimilarityScore::Exact(shaderType, other.shaderType), 8.0);
		score.Add(SimilarityScore::Exact(shaderProfile, other.shaderProfile), 3.0);
		score.Add(SimilarityScore::Exact(shaderEntryPoint, other.shaderEntryPoint), 2.0);
		score.Add(ModifiedShaderTargetDisk::CalculateSimilarityScore(targets, other.targets), 12.0);
		return score.Result();
	}

	double ModifiedShaderPackageDisk::CalculateSimilarityScore(const std::vector<ModifiedShaderPackageDisk>& left, const std::vector<ModifiedShaderPackageDisk>& right)
	{
		return SimilarityScore::CalculateCollectionSimilarityScore(left, right);
	}
}
