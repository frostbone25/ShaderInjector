#include "ModifiedShader.h"

#include <algorithm>
#include <unordered_set>

#include "Hash.h"
#include "ShaderInjectorIO.h"
#include "SimilarityScore.h"

namespace ModifiedShader
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

			return combinedHashes.empty() ? 1.0 : static_cast<double>(sharedHashCount) / static_cast<double>(combinedHashes.size());
		}

		bool AnalysesHaveSameStrictIdentity(const ShaderAnalysis::ShaderAnalysisDisk& left, const ShaderAnalysis::ShaderAnalysisDisk& right)
		{
			return left.succeeded && right.succeeded &&
				!left.crossVersionIdentityHash.empty() &&
				left.crossVersionIdentityHash == right.crossVersionIdentityHash;
		}
	}

	bool TargetDisk::MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const
	{
		if (shaderHash != 0)
		{
			for (const std::string& knownHash : knownShaderBytecodeHashes)
			{
				if (Hash::ParseHashText(knownHash) == shaderHash)
					return true;
			}
		}

		return AnalysesHaveSameStrictIdentity(shaderAnalysis, analysis);
	}

	double TargetDisk::CalculateSimilarityScore(const TargetDisk& other) const
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

	double TargetDisk::CalculateSimilarityScore(const std::vector<TargetDisk>& left, const std::vector<TargetDisk>& right)
	{
		return SimilarityScore::CalculateCollectionSimilarityScore(left, right);
	}

	bool PackageDisk::MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const
	{
		if (!enabled)
			return false;

		for (const TargetDisk& target : targets)
		{
			if (target.MatchesShader(shaderHash, analysis))
				return true;
		}

		return false;
	}

	double PackageDisk::CalculateSimilarityScore(const PackageDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(id, other.id), 2.0);
		score.Add(SimilarityScore::Exact(shaderType, other.shaderType), 8.0);
		score.Add(SimilarityScore::Exact(shaderProfile, other.shaderProfile), 3.0);
		score.Add(SimilarityScore::Exact(shaderEntryPoint, other.shaderEntryPoint), 2.0);
		score.Add(TargetDisk::CalculateSimilarityScore(targets, other.targets), 12.0);
		return score.Result();
	}

	double PackageDisk::CalculateSimilarityScore(const std::vector<PackageDisk>& left, const std::vector<PackageDisk>& right)
	{
		return SimilarityScore::CalculateCollectionSimilarityScore(left, right);
	}

	bool WriteJson(const PackageDisk& package)
	{
		if (package.jsonPath.empty())
			return false;

		PackageDisk portablePackage = package;
		portablePackage.sourceFile = ShaderInjectorIO::FileNameFromPath(portablePackage.sourceFile.empty() ? portablePackage.sourcePath : portablePackage.sourceFile);
		portablePackage.compiledBlobFile = ShaderInjectorIO::FileNameFromPath(portablePackage.compiledBlobFile.empty() ? portablePackage.compiledBlobPath : portablePackage.compiledBlobFile);
		portablePackage.packageDirectory = ".";
		portablePackage.jsonPath = ShaderInjectorIO::FileNameFromPath(package.jsonPath);
		portablePackage.sourcePath = portablePackage.sourceFile;
		portablePackage.compiledBlobPath = portablePackage.compiledBlobFile;

		nlohmann::ordered_json json = portablePackage;
		json.erase("packageDirectory");
		json.erase("jsonPath");
		json.erase("sourcePath");
		json.erase("compiledBlobPath");
		return ShaderInjectorIO::WriteTextFile(package.jsonPath, json.dump(4));
	}

	bool LoadJson(const std::string& jsonPath, PackageDisk& outPackage)
	{
		try
		{
			std::string jsonText;

			if (!ShaderInjectorIO::ReadTextFile(jsonPath, jsonText))
				return false;

			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);
			PackageDisk package = json.get<PackageDisk>();

			if (package.format != formatName)
				return false;

			package.jsonPath = jsonPath;
			package.packageDirectory = ShaderInjectorIO::DirectoryFromPath(jsonPath);
			package.sourceFile = ShaderInjectorIO::FileNameFromPath(package.sourceFile);
			package.sourcePath = package.sourceFile.empty() ? "" : ShaderInjectorIO::JoinPath(package.packageDirectory, package.sourceFile);
			package.compiledBlobFile = ShaderInjectorIO::FileNameFromPath(package.compiledBlobFile);
			package.compiledBlobPath = package.compiledBlobFile.empty() ? "" : ShaderInjectorIO::JoinPath(package.packageDirectory, package.compiledBlobFile);

			if (package.id.empty())
				package.id = ShaderInjectorIO::FileNameFromPath(package.packageDirectory);

			if (package.name.empty())
				package.name = package.id;

			outPackage = std::move(package);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	TargetDisk BuildTargetFromShaderTarget(
		const ShaderTarget::ShaderTargetDisk& shaderTarget,
		const std::string& targetApplication,
		const std::string& gameVersion)
	{
		TargetDisk target{};
		target.name = shaderTarget.name;
		target.targetApplication = targetApplication;
		target.gameVersion = gameVersion;
		target.originalShaderBytecodeLength = shaderTarget.originalShaderBytecodeLength;
		target.shaderAnalysis = shaderTarget.originalShaderAnalysis;

		auto addHash = [&target](const std::string& hash)
		{
			if (hash.empty() || std::find(target.knownShaderBytecodeHashes.begin(), target.knownShaderBytecodeHashes.end(), hash) != target.knownShaderBytecodeHashes.end())
				return;

			target.knownShaderBytecodeHashes.push_back(hash);
		};

		addHash(shaderTarget.originalShaderBytecodeHash);

		for (const std::string& aliasHash : shaderTarget.shaderBytecodeHashAliases)
			addHash(aliasHash);

		return target;
	}
}
