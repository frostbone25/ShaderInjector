#include "DiscoveryLogicMirror.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Hash.h"
#include "ModifiedShaderMatching.h"

namespace CrossVersionDiscoveryTest
{
	namespace
	{
		// ShaderDiscovery.cpp lines 22-23
		constexpr double maximumReplacementByteLengthDifferenceRatio = 0.05;

		// ShaderAutomaticDiscovery.cpp line 98
		constexpr size_t byteLengthTolerancePercent = 15;
	}

	bool HasPlausibleReplacementByteLength(size_t candidateLength, const ShaderTarget::ShaderTargetDisk& replacement)
	{
		if (replacement.originalShaderBytecodeLength.empty())
			return true;

		char* parseEnd = nullptr;
		const unsigned long long replacementLength = std::strtoull(replacement.originalShaderBytecodeLength.c_str(), &parseEnd, 10);
		if (!parseEnd || *parseEnd != '\0' || replacementLength == 0)
			return true;

		const double largerLength = static_cast<double>((std::max)(candidateLength, static_cast<size_t>(replacementLength)));
		const double difference = std::fabs(static_cast<double>(candidateLength) - static_cast<double>(replacementLength));
		return difference / largerLength <= maximumReplacementByteLengthDifferenceRatio;
	}

	bool HasStrictCrossVersionIdentity(const ShaderAnalysis::ShaderAnalysisDisk& analysis)
	{
		return analysis.succeeded &&
			!analysis.portableReflectionIdentityHash.empty() &&
			!analysis.semanticInstructionSetHash.empty() &&
			!analysis.crossVersionIdentityHash.empty();
	}

	bool WouldEnqueueByByteLength(ShaderTarget::ShaderType shaderType, size_t byteLength, const std::vector<size_t>& knownTargetLengths)
	{
		(void)shaderType;
		if (knownTargetLengths.empty())
			return true;

		for (size_t knownLength : knownTargetLengths)
		{
			if (knownLength == 0)
				continue;

			const size_t tolerance = (std::max<size_t>)(1, (knownLength * byteLengthTolerancePercent) / 100);
			const size_t minimumLength = knownLength > tolerance ? knownLength - tolerance : 1;
			const size_t maximumLength = knownLength + tolerance;
			if (byteLength >= minimumLength && byteLength <= maximumLength)
				return true;
		}
		return false;
	}

	ShaderTarget::ShaderTargetDisk MakeReplacementFromPackageTarget(
		const ModifiedShader::TargetDisk& target,
		ShaderTarget::ShaderType shaderType,
		const std::string& name)
	{
		ShaderTarget::ShaderTargetDisk replacement{};
		replacement.enabled = true;
		replacement.name = name;
		replacement.shaderType = shaderType;
		replacement.originalShaderBytecodeLength = target.originalShaderBytecodeLength;
		replacement.originalShaderAnalysis = target.shaderAnalysis;
		if (!target.knownShaderBytecodeHashes.empty())
			replacement.originalShaderBytecodeHash = target.knownShaderBytecodeHashes.front();
		return replacement;
	}

	ReplacementMatchResult EvaluateReplacementDiscovery(
		uint64_t shaderHash,
		ShaderTarget::ShaderType shaderType,
		size_t shaderBytecodeLength,
		const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis,
		const std::vector<ShaderTarget::ShaderTargetDisk>& replacements)
	{
		(void)shaderHash;
		ReplacementMatchResult result{};

		if (shaderBytecodeLength == 0)
			return result;

		result.hasStrictCandidateIdentity = HasStrictCrossVersionIdentity(candidateAnalysis);
		if (!result.hasStrictCandidateIdentity)
			return result;

		bool hasPlausibleReplacement = false;
		for (const ShaderTarget::ShaderTargetDisk& replacement : replacements)
		{
			if (replacement.enabled && replacement.shaderType == shaderType &&
				HasStrictCrossVersionIdentity(replacement.originalShaderAnalysis) &&
				HasPlausibleReplacementByteLength(shaderBytecodeLength, replacement))
			{
				hasPlausibleReplacement = true;
				break;
			}
		}

		if (!hasPlausibleReplacement)
			return result;

		int matchingReplacementIndex = -1;
		for (int replacementIndex = 0; replacementIndex < static_cast<int>(replacements.size()); ++replacementIndex)
		{
			const ShaderTarget::ShaderTargetDisk& replacement = replacements[replacementIndex];
			if (!replacement.enabled || replacement.shaderType != shaderType ||
				!HasPlausibleReplacementByteLength(shaderBytecodeLength, replacement))
			{
				continue;
			}

			result.passedLengthGate = true;

			if (replacement.originalShaderAnalysis.crossVersionIdentityHash != candidateAnalysis.crossVersionIdentityHash)
				continue;

			if (matchingReplacementIndex >= 0)
				return ReplacementMatchResult{};

			matchingReplacementIndex = replacementIndex;
		}

		if (matchingReplacementIndex >= 0)
		{
			result.replacementIndex = matchingReplacementIndex;
			result.accepted = true;
			result.matchedByExactIdentity = true;
			return result;
		}

		// ShaderDiscovery.cpp fuzzy fallback (after exact identity miss, before reject; ~line 254).
		double bestFuzzyScore = 0.0;
		double secondBestFuzzyScore = 0.0;
		int bestFuzzyReplacementIndex = -1;

		for (int replacementIndex = 0; replacementIndex < static_cast<int>(replacements.size()); ++replacementIndex)
		{
			const ShaderTarget::ShaderTargetDisk& replacement = replacements[replacementIndex];
			if (!replacement.enabled || replacement.shaderType != shaderType ||
				!HasPlausibleReplacementByteLength(shaderBytecodeLength, replacement) ||
				!HasStrictCrossVersionIdentity(replacement.originalShaderAnalysis))
			{
				continue;
			}

			if (replacement.originalShaderAnalysis.portableReflectionIdentityHash !=
				candidateAnalysis.portableReflectionIdentityHash)
			{
				continue;
			}

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

		result.bestFuzzyScore = bestFuzzyScore;
		result.secondBestFuzzyScore = secondBestFuzzyScore;

		if (bestFuzzyReplacementIndex >= 0 &&
			bestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE &&
			bestFuzzyScore - secondBestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN)
		{
			result.replacementIndex = bestFuzzyReplacementIndex;
			result.accepted = true;
			result.matchedByFuzzySimilarity = true;
		}

		return result;
	}

	ModifiedShaderMatchResult EvaluateModifiedShaderDiscovery(
		uint64_t shaderHash,
		ShaderTarget::ShaderType shaderType,
		size_t shaderBytecodeLength,
		const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis,
		const std::vector<ModifiedShader::PackageDisk>& modifiedShaders)
	{
		(void)shaderBytecodeLength;
		ModifiedShaderMatchResult result{};

		if (shaderHash == 0)
			return result;

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
			return result;

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
						break;
					}
				}
				if (packageMatchesHash)
					break;
			}

			if (!packageMatchesHash)
				continue;

			if (hashMatchIndex >= 0)
				return ModifiedShaderMatchResult{};

			hashMatchIndex = packageIndex;
		}

		if (hashMatchIndex >= 0)
		{
			result.accepted = true;
			result.packageIndex = hashMatchIndex;
			result.matchedByHash = true;
			return result;
		}
#endif

#if SHADER_INJECTOR_DISCOVERY_MATCH_MODIFIED_SHADER_BY_ANALYSIS
		if (!candidateAnalysis.succeeded)
			return result;

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
				candidateTarget.originalShaderBytecodeLength = std::to_string(shaderBytecodeLength);
				candidateTarget.shaderAnalysis = candidateAnalysis;
				packageScore = (std::max)(
					packageScore,
					ModifiedShaderMatching::CalculateTargetSimilarityScore(referenceTarget, candidateTarget));
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

		result.bestScore = bestScore;
		result.secondBestScore = secondBestScore;
		result.packageIndex = bestPackageIndex;

		if (bestPackageIndex >= 0 &&
			bestScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE &&
			bestScore - secondBestScore >= SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN)
		{
			result.accepted = true;
		}
#endif

		return result;
	}
}
