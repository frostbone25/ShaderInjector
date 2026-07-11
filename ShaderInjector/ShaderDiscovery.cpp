#include "ShaderDiscovery.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "Hash.h"
#include "ShaderAnalyzer.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"

namespace ShaderDiscovery
{
	namespace
	{
		// The validated old/new sample pairs differ by less than one percent. A five
		// percent window leaves room for ordinary compiler drift while keeping the
		// one-time semantic analysis candidate set small.
		constexpr double maximumByteLengthDifferenceRatio = 0.05;

		struct ShaderKey
		{
			uint64_t hash = 0;
			ShaderTarget::ShaderType type = ShaderTarget::Unknown;

			bool operator==(const ShaderKey& other) const
			{
				return hash == other.hash && type == other.type;
			}
		};

		struct ShaderKeyHasher
		{
			size_t operator()(const ShaderKey& key) const
			{
				return static_cast<size_t>(key.hash ^ (static_cast<uint64_t>(key.type) << 57));
			}
		};

		std::unordered_map<ShaderKey, int, ShaderKeyHasher> gDiscoveredReplacementAliases;
		std::unordered_set<ShaderKey, ShaderKeyHasher> gAttemptedCandidates;
		std::unordered_map<ShaderKey, ShaderAnalysis::ShaderAnalysisDisk, ShaderKeyHasher> gReplacementCandidateAnalyses;
		std::unordered_map<ShaderKey, ShaderAnalysis::ShaderAnalysisDisk, ShaderKeyHasher> gModifiedCandidateAnalyses;
		std::unordered_map<ShaderKey, std::string, ShaderKeyHasher> gDiscoveredModifiedShaders;
		std::unordered_set<ShaderKey, ShaderKeyHasher> gAttemptedModifiedShaders;
		std::mutex gModifiedDiscoveryMutex;

		bool HasPlausibleByteLength(size_t candidateLength, const ShaderTarget::ShaderTargetDisk& replacement)
		{
			if (replacement.originalShaderBytecodeLength.empty())
				return true;

			char* parseEnd = nullptr;
			const unsigned long long replacementLength = std::strtoull(replacement.originalShaderBytecodeLength.c_str(), &parseEnd, 10);
			if (!parseEnd || *parseEnd != '\0' || replacementLength == 0)
				return true;

			const double largerLength = static_cast<double>((std::max)(candidateLength, static_cast<size_t>(replacementLength)));
			const double difference = std::fabs(static_cast<double>(candidateLength) - static_cast<double>(replacementLength));
			return difference / largerLength <= maximumByteLengthDifferenceRatio;
		}

		bool HasStrictCrossVersionIdentity(const ShaderAnalysis::ShaderAnalysisDisk& analysis)
		{
			return analysis.succeeded &&
				!analysis.portableReflectionIdentityHash.empty() &&
				!analysis.semanticInstructionSetHash.empty() &&
				!analysis.crossVersionIdentityHash.empty();
		}
	}

	void ResetRuntimeCache()
	{
		gDiscoveredReplacementAliases.clear();
		gAttemptedCandidates.clear();
		gReplacementCandidateAnalyses.clear();
		{
			std::lock_guard<std::mutex> lock(gModifiedDiscoveryMutex);
			gModifiedCandidateAnalyses.clear();
			gDiscoveredModifiedShaders.clear();
			gAttemptedModifiedShaders.clear();
		}
	}

	bool EnsureReplacementAnalysis(ShaderTarget::ShaderTargetDisk& replacement)
	{
		if (HasStrictCrossVersionIdentity(replacement.originalShaderAnalysis))
			return true;

		std::vector<uint8_t> originalBytecode;
		if (replacement.originalShaderBlobPath.empty() ||
			!ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.originalShaderBlobPath, originalBytecode) ||
			originalBytecode.empty())
		{
			return false;
		}

		if (!ShaderAnalyzer::Analyze(originalBytecode.data(), originalBytecode.size(), replacement.originalShaderAnalysis))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"ShaderDiscovery->EnsureReplacementAnalysis: failed for " + replacement.name +
				": " + replacement.originalShaderAnalysis.error);
			return false;
		}

		replacement.schemaVersion = 5;
		if (!ShaderTarget::WriteShaderTargetJson(replacement))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"ShaderDiscovery->EnsureReplacementAnalysis: could not persist analysis for " + replacement.name);
		}

		return HasStrictCrossVersionIdentity(replacement.originalShaderAnalysis);
	}

	bool PersistShaderHashAlias(ShaderTarget::ShaderTargetDisk& replacement, uint64_t shaderHash)
	{
		if (shaderHash == 0 || Hash::ParseHashText(replacement.originalShaderBytecodeHash) == shaderHash)
			return false;

		for (const std::string& aliasHash : replacement.shaderBytecodeHashAliases)
		{
			if (Hash::ParseHashText(aliasHash) == shaderHash)
				return false;
		}

		replacement.shaderBytecodeHashAliases.push_back(Hash::FormatHash(shaderHash));
		replacement.schemaVersion = 5;
		if (!ShaderTarget::WriteShaderTargetJson(replacement))
		{
			replacement.shaderBytecodeHashAliases.pop_back();
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"ShaderDiscovery->PersistShaderHashAlias: failed to save alias for " + replacement.name);
			return false;
		}

		ShaderInjectorGUI::WriteToRuntimeLogSuccess(
			"ShaderDiscovery->PersistShaderHashAlias: saved " + Hash::FormatHash(shaderHash) +
			" for " + replacement.name);
		return true;
	}

	int DiscoverEnabledReplacement(
		uint64_t shaderHash,
		ShaderTarget::ShaderType shaderType,
		const std::vector<uint8_t>& shaderBytecode,
		const std::vector<ShaderTarget::ShaderTargetDisk>& replacements)
	{
		if (shaderHash == 0 || shaderBytecode.empty())
			return -1;

		const ShaderKey candidateKey{ shaderHash, shaderType };
		const auto discoveredAlias = gDiscoveredReplacementAliases.find(candidateKey);
		if (discoveredAlias != gDiscoveredReplacementAliases.end())
			return discoveredAlias->second;
		if (gAttemptedCandidates.find(candidateKey) != gAttemptedCandidates.end())
			return -1;

		bool hasPlausibleReplacement = false;
		for (const ShaderTarget::ShaderTargetDisk& replacement : replacements)
		{
			if (replacement.enabled && replacement.shaderType == shaderType &&
				HasStrictCrossVersionIdentity(replacement.originalShaderAnalysis) &&
				HasPlausibleByteLength(shaderBytecode.size(), replacement))
			{
				hasPlausibleReplacement = true;
				break;
			}
		}

		if (!hasPlausibleReplacement)
		{
			gAttemptedCandidates.insert(candidateKey);
			return -1;
		}

		ShaderAnalysis::ShaderAnalysisDisk candidateAnalysis{};
		const auto cachedAnalysis = gReplacementCandidateAnalyses.find(candidateKey);
		if (cachedAnalysis != gReplacementCandidateAnalyses.end())
		{
			candidateAnalysis = cachedAnalysis->second;
		}
		else
		{
			ShaderAnalyzer::Analyze(shaderBytecode.data(), shaderBytecode.size(), candidateAnalysis);
			gReplacementCandidateAnalyses.emplace(candidateKey, candidateAnalysis);
		}

		if (!HasStrictCrossVersionIdentity(candidateAnalysis))
		{
			std::string failureReason;
			if (!candidateAnalysis.succeeded)
			{
				failureReason = candidateAnalysis.error.empty() ?
					"analysis did not succeed" : candidateAnalysis.error;
			}
			else
			{
				auto appendMissingField = [&](const char* fieldName)
				{
					if (!failureReason.empty())
						failureReason += ", ";
					failureReason += fieldName;
				};
				if (candidateAnalysis.portableReflectionIdentityHash.empty())
					appendMissingField("portableReflectionIdentityHash");
				if (candidateAnalysis.semanticInstructionSetHash.empty())
					appendMissingField("semanticInstructionSetHash");
				if (candidateAnalysis.crossVersionIdentityHash.empty())
					appendMissingField("crossVersionIdentityHash");
				if (failureReason.empty())
					failureReason = "unknown";
			}

			ShaderInjectorGUI::WriteToRuntimeLogError(
				"ShaderDiscovery->DiscoverEnabledReplacement: " +
				std::string(candidateAnalysis.succeeded ? "incomplete cross-version identity for " : "analysis failed for ") +
				Hash::FormatHash(shaderHash) +
				" type=" + StringHelper::ShaderTypeToString(shaderType) +
				": " + failureReason);
			gAttemptedCandidates.insert(candidateKey);
			return -1;
		}

		int matchingReplacementIndex = -1;
		for (int replacementIndex = 0; replacementIndex < static_cast<int>(replacements.size()); ++replacementIndex)
		{
			const ShaderTarget::ShaderTargetDisk& replacement = replacements[replacementIndex];
			if (!replacement.enabled || replacement.shaderType != shaderType ||
				!HasPlausibleByteLength(shaderBytecode.size(), replacement))
			{
				continue;
			}

			if (replacement.originalShaderAnalysis.crossVersionIdentityHash != candidateAnalysis.crossVersionIdentityHash)
				continue;

			if (matchingReplacementIndex >= 0)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError(
					"ShaderDiscovery->DiscoverEnabledReplacement: ambiguous exact identity " +
					candidateAnalysis.crossVersionIdentityHash + " for shader " + Hash::FormatHash(shaderHash));
				gAttemptedCandidates.insert(candidateKey);
				return -1;
			}

			matchingReplacementIndex = replacementIndex;
		}

		if (matchingReplacementIndex < 0)
		{
			double bestFuzzyScore = 0.0;
			double secondBestFuzzyScore = 0.0;
			int bestFuzzyReplacementIndex = -1;
			bool hasReflectionGateCandidate = false;

			for (int replacementIndex = 0; replacementIndex < static_cast<int>(replacements.size()); ++replacementIndex)
			{
				const ShaderTarget::ShaderTargetDisk& replacement = replacements[replacementIndex];
				if (!replacement.enabled || replacement.shaderType != shaderType ||
					!HasPlausibleByteLength(shaderBytecode.size(), replacement) ||
					!HasStrictCrossVersionIdentity(replacement.originalShaderAnalysis))
				{
					continue;
				}

				if (replacement.originalShaderAnalysis.portableReflectionIdentityHash !=
					candidateAnalysis.portableReflectionIdentityHash)
				{
					continue;
				}

				hasReflectionGateCandidate = true;
				const double replacementScore =
					replacement.originalShaderAnalysis.CalculateSimilarityScore(candidateAnalysis);

				if (replacementScore > bestFuzzyScore)
				{
					secondBestFuzzyScore = bestFuzzyScore;
					bestFuzzyScore = replacementScore;
					bestFuzzyReplacementIndex = replacementIndex;
				}
				else if (replacementScore > secondBestFuzzyScore)
				{
					secondBestFuzzyScore = replacementScore;
				}
			}

			if (bestFuzzyReplacementIndex >= 0 &&
				bestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE &&
				bestFuzzyScore - secondBestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN)
			{
				gDiscoveredReplacementAliases.emplace(candidateKey, bestFuzzyReplacementIndex);
				ShaderInjectorGUI::WriteToRuntimeLogSuccess(
					"ShaderDiscovery->DiscoverEnabledReplacement: fuzzy cross-version shader identity matched " +
					Hash::FormatHash(shaderHash) + " -> " + replacements[bestFuzzyReplacementIndex].name +
					" score=" + std::to_string(bestFuzzyScore) +
					" portableReflection=" + candidateAnalysis.portableReflectionIdentityHash);
				return bestFuzzyReplacementIndex;
			}

			if (hasReflectionGateCandidate)
			{
				std::string identityDiff;
				if (bestFuzzyReplacementIndex >= 0)
				{
					const ShaderAnalysis::ShaderAnalysisDisk& bestAnalysis =
						replacements[bestFuzzyReplacementIndex].originalShaderAnalysis;

					auto appendDiff = [&](const char* label, const std::string& left, const std::string& right)
					{
						if (left != right)
						{
							if (!identityDiff.empty())
								identityDiff += ", ";
							identityDiff += std::string(label) + "=" + left + "!=" + right;
						}
					};
					appendDiff("portableReflection",
						candidateAnalysis.portableReflectionIdentityHash,
						bestAnalysis.portableReflectionIdentityHash);
					appendDiff("semanticInstructionSet",
						candidateAnalysis.semanticInstructionSetHash,
						bestAnalysis.semanticInstructionSetHash);
					appendDiff("crossVersionIdentity",
						candidateAnalysis.crossVersionIdentityHash,
						bestAnalysis.crossVersionIdentityHash);
					appendDiff("entryFunction",
						candidateAnalysis.entryFunctionName,
						bestAnalysis.entryFunctionName);
				}

				ShaderInjectorGUI::WriteToRuntimeLogError(
					"ShaderDiscovery->DiscoverEnabledReplacement: fuzzy match rejected for " +
					Hash::FormatHash(shaderHash) +
					" bestScore=" + std::to_string(bestFuzzyScore) +
					" secondBestScore=" + std::to_string(secondBestFuzzyScore) +
					(identityDiff.empty() ? "" : " identityDiff={" + identityDiff + "}") +
					(bestFuzzyReplacementIndex >= 0 ?
						" bestCandidate=" + replacements[bestFuzzyReplacementIndex].name : ""));
			}

			gAttemptedCandidates.insert(candidateKey);
			return -1;
		}

		gDiscoveredReplacementAliases.emplace(candidateKey, matchingReplacementIndex);
		ShaderInjectorGUI::WriteToRuntimeLogSuccess(
			"ShaderDiscovery->DiscoverEnabledReplacement: exact cross-version shader identity matched " +
			Hash::FormatHash(shaderHash) + " -> " + replacements[matchingReplacementIndex].name +
			" identity=" + candidateAnalysis.crossVersionIdentityHash);
		return matchingReplacementIndex;
	}

	int DiscoverEnabledModifiedShader(
		uint64_t shaderHash,
		ShaderTarget::ShaderType shaderType,
		const std::vector<uint8_t>& shaderBytecode,
		const std::vector<ModifiedShader::PackageDisk>& modifiedShaders,
		ShaderAnalysis::ShaderAnalysisDisk* outCandidateAnalysis)
	{
		if (outCandidateAnalysis)
			*outCandidateAnalysis = ShaderAnalysis::ShaderAnalysisDisk{};

		std::lock_guard<std::mutex> lock(gModifiedDiscoveryMutex);
		if (shaderHash == 0 || shaderBytecode.empty())
			return -1;

		const ShaderKey candidateKey{ shaderHash, shaderType };
		const auto cachedMatch = gDiscoveredModifiedShaders.find(candidateKey);
		if (cachedMatch != gDiscoveredModifiedShaders.end())
		{
			for (int index = 0; index < static_cast<int>(modifiedShaders.size()); ++index)
			{
				if (modifiedShaders[index].id == cachedMatch->second && modifiedShaders[index].enabled)
					return index;
			}
		}

		if (gAttemptedModifiedShaders.find(candidateKey) != gAttemptedModifiedShaders.end())
			return -1;

		bool hasCompatiblePackage = false;
		for (const ModifiedShader::PackageDisk& package : modifiedShaders)
		{
			if (package.enabled && package.shaderType == shaderType && !package.targets.empty())
			{
				hasCompatiblePackage = true;
				break;
			}
		}
		if (!hasCompatiblePackage)
		{
			gAttemptedModifiedShaders.insert(candidateKey);
			return -1;
		}

#if SHADER_INJECTOR_DISCOVERY_MATCH_MODIFIED_SHADER_BY_HASH
		int hashMatchIndex = -1;
		for (int packageIndex = 0; packageIndex < static_cast<int>(modifiedShaders.size()); ++packageIndex)
		{
			const ModifiedShader::PackageDisk& package = modifiedShaders[packageIndex];
			if (!package.enabled || package.shaderType != shaderType)
				continue;

			bool packageMatchesHash = false;
			for (const ModifiedShader::TargetDisk& target : package.targets)
			{
				for (const std::string& knownHash : target.knownShaderBytecodeHashes)
				{
					if (Hash::ParseHashText(knownHash) == shaderHash)
					{
						packageMatchesHash = true;
						if (outCandidateAnalysis && target.shaderAnalysis.succeeded)
							*outCandidateAnalysis = target.shaderAnalysis;
						break;
					}
				}
				if (packageMatchesHash)
					break;
			}

			if (!packageMatchesHash)
				continue;

			if (hashMatchIndex >= 0)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError(
					"ShaderDiscovery->DiscoverEnabledModifiedShader: ambiguous direct hash match for " +
					Hash::FormatHash(shaderHash));
				gAttemptedModifiedShaders.insert(candidateKey);
				return -1;
			}
			hashMatchIndex = packageIndex;
		}

		if (hashMatchIndex >= 0)
		{
			gDiscoveredModifiedShaders[candidateKey] = modifiedShaders[hashMatchIndex].id;
			ShaderInjectorGUI::WriteToRuntimeLogSuccess(
				"ShaderDiscovery->DiscoverEnabledModifiedShader: direct hash match " +
				Hash::FormatHash(shaderHash) + " -> " + modifiedShaders[hashMatchIndex].id);
			return hashMatchIndex;
		}
#endif

#if SHADER_INJECTOR_DISCOVERY_MATCH_MODIFIED_SHADER_BY_ANALYSIS
		ShaderAnalysis::ShaderAnalysisDisk candidateAnalysis{};
		const auto cachedAnalysis = gModifiedCandidateAnalyses.find(candidateKey);
		if (cachedAnalysis != gModifiedCandidateAnalyses.end())
			candidateAnalysis = cachedAnalysis->second;
		else
		{
			ShaderAnalyzer::Analyze(shaderBytecode.data(), shaderBytecode.size(), candidateAnalysis);
			gModifiedCandidateAnalyses.emplace(candidateKey, candidateAnalysis);
		}

		if (candidateAnalysis.succeeded)
		{
			if (outCandidateAnalysis)
				*outCandidateAnalysis = candidateAnalysis;

			double bestScore = 0.0;
			double secondBestScore = 0.0;
			int bestPackageIndex = -1;

			for (int packageIndex = 0; packageIndex < static_cast<int>(modifiedShaders.size()); ++packageIndex)
			{
				const ModifiedShader::PackageDisk& package = modifiedShaders[packageIndex];
				if (!package.enabled || package.shaderType != shaderType)
					continue;

				double packageScore = 0.0;
				for (const ModifiedShader::TargetDisk& storedTarget : package.targets)
				{
					ModifiedShader::TargetDisk referenceTarget = storedTarget;
					referenceTarget.knownShaderBytecodeHashes.clear();
					ModifiedShader::TargetDisk candidateTarget{};
					candidateTarget.targetApplication = referenceTarget.targetApplication;
					candidateTarget.gameVersion = referenceTarget.gameVersion;
					candidateTarget.originalShaderBytecodeLength = std::to_string(shaderBytecode.size());
					candidateTarget.shaderAnalysis = candidateAnalysis;
					packageScore = (std::max)(packageScore, referenceTarget.CalculateSimilarityScore(candidateTarget));
				}

				if (packageScore > bestScore)
				{
					secondBestScore = bestScore;
					bestScore = packageScore;
					bestPackageIndex = packageIndex;
				}
				else if (packageScore > secondBestScore)
				{
					secondBestScore = packageScore;
				}
			}

			if (bestPackageIndex >= 0 && bestScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE &&
				bestScore - secondBestScore >= SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN)
			{
				gDiscoveredModifiedShaders[candidateKey] = modifiedShaders[bestPackageIndex].id;
				ShaderInjectorGUI::WriteToRuntimeLogSuccess(
					"ShaderDiscovery->DiscoverEnabledModifiedShader: analysis match " +
					Hash::FormatHash(shaderHash) + " -> " + modifiedShaders[bestPackageIndex].id +
					" score=" + std::to_string(bestScore));
				return bestPackageIndex;
			}

			if (bestPackageIndex >= 0)
			{
				const ModifiedShader::PackageDisk& bestPackage = modifiedShaders[bestPackageIndex];
				const ShaderAnalysis::ShaderAnalysisDisk* bestTargetAnalysis = nullptr;
				double bestTargetScore = 0.0;
				for (const ModifiedShader::TargetDisk& storedTarget : bestPackage.targets)
				{
					ModifiedShader::TargetDisk referenceTarget = storedTarget;
					referenceTarget.knownShaderBytecodeHashes.clear();
					ModifiedShader::TargetDisk candidateTarget{};
					candidateTarget.targetApplication = referenceTarget.targetApplication;
					candidateTarget.gameVersion = referenceTarget.gameVersion;
					candidateTarget.originalShaderBytecodeLength = std::to_string(shaderBytecode.size());
					candidateTarget.shaderAnalysis = candidateAnalysis;
					const double targetScore = referenceTarget.CalculateSimilarityScore(candidateTarget);
					if (targetScore > bestTargetScore)
					{
						bestTargetScore = targetScore;
						bestTargetAnalysis = &storedTarget.shaderAnalysis;
					}
				}

				std::string identityDiff;
				if (bestTargetAnalysis)
				{
					auto appendDiff = [&](const char* label, const std::string& left, const std::string& right)
					{
						if (left != right)
						{
							if (!identityDiff.empty())
								identityDiff += ", ";
							identityDiff += std::string(label) + "=" + left + "!=" + right;
						}
					};
					appendDiff("portableReflection",
						candidateAnalysis.portableReflectionIdentityHash,
						bestTargetAnalysis->portableReflectionIdentityHash);
					appendDiff("semanticInstructionSet",
						candidateAnalysis.semanticInstructionSetHash,
						bestTargetAnalysis->semanticInstructionSetHash);
					appendDiff("crossVersionIdentity",
						candidateAnalysis.crossVersionIdentityHash,
						bestTargetAnalysis->crossVersionIdentityHash);
					appendDiff("entryFunction",
						candidateAnalysis.entryFunctionName,
						bestTargetAnalysis->entryFunctionName);
				}

				ShaderInjectorGUI::WriteToRuntimeLogError(
					"ShaderDiscovery->DiscoverEnabledModifiedShader: analysis match rejected for " +
					Hash::FormatHash(shaderHash) +
					" bestScore=" + std::to_string(bestScore) +
					" secondBestScore=" + std::to_string(secondBestScore) +
					(identityDiff.empty() ? "" : " identityDiff={" + identityDiff + "}") +
					" bestCandidate=" + bestPackage.id);
			}
		}
		else
		{
			const std::string failureReason = candidateAnalysis.error.empty() ?
				"analysis did not succeed" : candidateAnalysis.error;
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"ShaderDiscovery->DiscoverEnabledModifiedShader: analysis failed for " +
				Hash::FormatHash(shaderHash) +
				" type=" + StringHelper::ShaderTypeToString(shaderType) +
				": " + failureReason);
		}
#endif

		gAttemptedModifiedShaders.insert(candidateKey);
		return -1;
	}
}
