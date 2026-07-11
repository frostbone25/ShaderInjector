#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../ThirdParty/doctest.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "DiscoveryLogicMirror.h"
#include "FixtureLoader.h"
#include "Hash.h"
#include "ModifiedShaderMatching.h"
#include "ShaderDiscovery.h"
#include "SimilarityDiagnostics.h"

namespace
{
	struct ComparisonReport
	{
		std::string caseName;
		bool rawHashEqual = false;
		bool portableReflectionEqual = false;
		bool semanticInstructionSetEqual = false;
		bool crossVersionIdentityEqual = false;
		double similarityScore = 0.0;
		std::vector<std::string> hurtFields;
		bool modifiedShaderAccept = false;
		bool replacementAccept = false;
		bool enqueue15Percent = false;
		bool replacementLengthGate5Percent = false;
		std::string notes;
	};

	struct LabeledPackage
	{
		std::string label;
		ModifiedShader::PackageDisk package;
	};

	struct PackageScoreEntry
	{
		std::string label;
		double score = 0.0;
	};

	struct AmbiguityReport
	{
		std::string caseName;
		std::vector<PackageScoreEntry> packageScores;
		std::string winnerLabel;
		double bestScore = 0.0;
		double secondBestScore = 0.0;
		double margin = 0.0;
		bool clearsAmbiguityMargin = false;
		bool modifiedShaderAccept = false;
		int winningPackageIndex = -1;
		std::string notes;
	};

	double ScorePackageAgainstCandidate(
		const ModifiedShader::PackageDisk& package,
		size_t candidateByteLength,
		const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis)
	{
		double packageScore = 0.0;
		for (const ModifiedShader::TargetDisk& storedTarget : package.targets)
		{
			ModifiedShader::TargetDisk referenceTarget = storedTarget;
			referenceTarget.knownShaderBytecodeHashes.clear();
			ModifiedShader::TargetDisk candidateTarget{};
			candidateTarget.targetApplication = referenceTarget.targetApplication;
			candidateTarget.gameVersion = referenceTarget.gameVersion;
			candidateTarget.originalShaderBytecodeLength = std::to_string(candidateByteLength);
			candidateTarget.shaderAnalysis = candidateAnalysis;
			packageScore = (std::max)(
				packageScore,
				ModifiedShaderMatching::CalculateTargetSimilarityScore(referenceTarget, candidateTarget));
		}
		return packageScore;
	}

	void PrintReport(const ComparisonReport& report)
	{
		std::cout << "\n=== " << report.caseName << " ===\n";
		std::cout << "raw hash equal? " << (report.rawHashEqual ? "yes" : "no") << "\n";
		std::cout << "portable reflection equal? " << (report.portableReflectionEqual ? "yes" : "no") << "\n";
		std::cout << "semantic instruction set equal? " << (report.semanticInstructionSetEqual ? "yes" : "no") << "\n";
		std::cout << "crossVersionIdentity equal? " << (report.crossVersionIdentityEqual ? "yes" : "no") << "\n";
		std::cout << "similarity score: " << report.similarityScore << "\n";
		if (!report.hurtFields.empty())
		{
			std::cout << "sub-scores that hurt: ";
			for (size_t index = 0; index < report.hurtFields.size(); ++index)
			{
				if (index > 0)
					std::cout << ", ";
				std::cout << report.hurtFields[index];
			}
			std::cout << "\n";
		}
		std::cout << "DiscoverEnabledModifiedShader accept? " << (report.modifiedShaderAccept ? "yes" : "no") << "\n";
		std::cout << "DiscoverEnabledReplacement accept? " << (report.replacementAccept ? "yes" : "no") << "\n";
		std::cout << "length gate enqueue (±15%)? " << (report.enqueue15Percent ? "yes" : "no") << "\n";
		std::cout << "length gate replacement (±5%)? " << (report.replacementLengthGate5Percent ? "yes" : "no") << "\n";
		if (!report.notes.empty())
			std::cout << "notes: " << report.notes << "\n";
	}

	void PrintAmbiguityReport(const AmbiguityReport& report)
	{
		std::cout << "\n=== " << report.caseName << " ===\n";
		for (const PackageScoreEntry& entry : report.packageScores)
			std::cout << "package score [" << entry.label << "]: " << entry.score << "\n";
		std::cout << "winner: " << report.winnerLabel << " (" << report.bestScore << ")\n";
		std::cout << "runner-up score: " << report.secondBestScore << "\n";
		std::cout << "margin over runner-up: " << report.margin << "\n";
		std::cout << "ambiguity margin threshold: " << SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN << "\n";
		std::cout << "margin clears threshold? " << (report.clearsAmbiguityMargin ? "yes" : "no") << "\n";
		std::cout << "minimum similarity threshold: " << SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE << "\n";
		std::cout << "DiscoverEnabledModifiedShader accept? " << (report.modifiedShaderAccept ? "yes" : "no") << "\n";
		if (!report.notes.empty())
			std::cout << "notes: " << report.notes << "\n";
	}

	AmbiguityReport EvaluateAmbiguity(
		const std::string& caseName,
		const std::vector<LabeledPackage>& labeledPackages,
		size_t candidateByteLength,
		const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis,
		uint64_t candidateHash,
		const std::string& notes = {})
	{
		AmbiguityReport report{};
		report.caseName = caseName;
		report.notes = notes;

		std::vector<ModifiedShader::PackageDisk> packages;
		packages.reserve(labeledPackages.size());
		for (const LabeledPackage& labeledPackage : labeledPackages)
		{
			packages.push_back(labeledPackage.package);
			report.packageScores.push_back({
				labeledPackage.label,
				ScorePackageAgainstCandidate(labeledPackage.package, candidateByteLength, candidateAnalysis)});
		}

		std::sort(report.packageScores.begin(), report.packageScores.end(),
			[](const PackageScoreEntry& left, const PackageScoreEntry& right)
			{
				return left.score > right.score;
			});

		if (!report.packageScores.empty())
		{
			report.winnerLabel = report.packageScores.front().label;
			report.bestScore = report.packageScores.front().score;
		}
		if (report.packageScores.size() > 1)
			report.secondBestScore = report.packageScores[1].score;
		report.margin = report.bestScore - report.secondBestScore;
		report.clearsAmbiguityMargin =
			report.margin >= SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN;

		const auto discoveryResult = CrossVersionDiscoveryTest::EvaluateModifiedShaderDiscovery(
			candidateHash,
			ShaderTarget::PixelShader,
			candidateByteLength,
			candidateAnalysis,
			packages);
		report.modifiedShaderAccept = discoveryResult.accepted;
		report.winningPackageIndex = discoveryResult.packageIndex;

		PrintAmbiguityReport(report);
		return report;
	}

	ComparisonReport CompareCaptures(
		const std::string& caseName,
		const CrossVersionDiscoveryTest::ShaderCapture& left,
		const CrossVersionDiscoveryTest::ShaderCapture& right,
		const std::vector<ModifiedShader::PackageDisk>& packages,
		const std::vector<ShaderTarget::ShaderTargetDisk>& replacements,
		size_t candidateByteLength,
		const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis,
		uint64_t candidateHash,
		const std::vector<size_t>& knownTargetLengths,
		const std::string& notes = {})
	{
		ComparisonReport report{};
		report.caseName = caseName;
		report.rawHashEqual = left.rawHash == right.rawHash;
		report.portableReflectionEqual =
			left.analysis.portableReflectionIdentityHash == right.analysis.portableReflectionIdentityHash;
		report.semanticInstructionSetEqual =
			left.analysis.semanticInstructionSetHash == right.analysis.semanticInstructionSetHash;
		report.crossVersionIdentityEqual =
			left.analysis.crossVersionIdentityHash == right.analysis.crossVersionIdentityHash;

		const CrossVersionDiscoveryTest::SimilarityDiagnostics diagnostics =
			CrossVersionDiscoveryTest::AnalyzeShaderSimilarity(left.analysis, right.analysis);
		report.similarityScore = diagnostics.totalScore;
		report.hurtFields = diagnostics.lowScoringFields();

		const auto modifiedResult = CrossVersionDiscoveryTest::EvaluateModifiedShaderDiscovery(
			candidateHash,
			ShaderTarget::PixelShader,
			candidateByteLength,
			candidateAnalysis,
			packages);
		report.modifiedShaderAccept = modifiedResult.accepted;

		const auto replacementResult = CrossVersionDiscoveryTest::EvaluateReplacementDiscovery(
			candidateHash,
			ShaderTarget::PixelShader,
			candidateByteLength,
			candidateAnalysis,
			replacements);
		report.replacementAccept = replacementResult.accepted;
		if (!replacements.empty())
		{
			report.replacementLengthGate5Percent = replacementResult.passedLengthGate ||
				CrossVersionDiscoveryTest::HasPlausibleReplacementByteLength(candidateByteLength, replacements.front());
		}
		report.enqueue15Percent = CrossVersionDiscoveryTest::WouldEnqueueByByteLength(
			ShaderTarget::PixelShader,
			candidateByteLength,
			knownTargetLengths);
		report.notes = notes;

		PrintReport(report);
		return report;
	}

	std::vector<ShaderTarget::ShaderTargetDisk> ReplacementsFromPackage(const ModifiedShader::PackageDisk& package)
	{
		std::vector<ShaderTarget::ShaderTargetDisk> replacements;
		for (const ModifiedShader::TargetDisk& target : package.targets)
		{
			replacements.push_back(CrossVersionDiscoveryTest::MakeReplacementFromPackageTarget(
				target,
				package.shaderType,
				package.name));
		}
		return replacements;
	}

	std::vector<size_t> KnownTargetLengths(const ModifiedShader::PackageDisk& package)
	{
		std::vector<size_t> lengths;
		for (const ModifiedShader::TargetDisk& target : package.targets)
		{
			if (!target.originalShaderBytecodeLength.empty())
				lengths.push_back(static_cast<size_t>(std::stoull(target.originalShaderBytecodeLength)));
		}
		return lengths;
	}

	// Constructed near-tie decoy layered on real package data: an exact in-memory
	// clone of the correct 1.005 Local Light package with a different id/name.
	// Both enabled packages score identically against the 1.004 runtime candidate,
	// producing a zero margin and triggering ambiguity rejection.
	ModifiedShader::PackageDisk MakeConstructedNearTieDecoy(const ModifiedShader::PackageDisk& correctPackage)
	{
		ModifiedShader::PackageDisk decoy = correctPackage;
		decoy.id = correctPackage.id + "_decoy";
		decoy.name = correctPackage.name + "_decoy";
		return decoy;
	}

	// Simulates compile drift that flips crossVersionIdentityHash while reflection
	// and the rest of the fingerprint stay aligned (the scenario exact-only matching misses).
	ShaderAnalysis::ShaderAnalysisDisk SimulateCrossVersionIdentityDrift(
		const ShaderAnalysis::ShaderAnalysisDisk& analysis)
	{
		ShaderAnalysis::ShaderAnalysisDisk drifted = analysis;
		drifted.crossVersionIdentityHash = "0000000000000001";
		return drifted;
	}

	void PrintReplacementFuzzyReport(
		const std::string& caseName,
		const CrossVersionDiscoveryTest::ReplacementMatchResult& result,
		const std::string& notes = {})
	{
		std::cout << "\n=== " << caseName << " ===\n";
		std::cout << "DiscoverEnabledReplacement accept? " << (result.accepted ? "yes" : "no") << "\n";
		std::cout << "matched by exact identity? " << (result.matchedByExactIdentity ? "yes" : "no") << "\n";
		std::cout << "matched by fuzzy similarity? " << (result.matchedByFuzzySimilarity ? "yes" : "no") << "\n";
		std::cout << "best fuzzy score: " << result.bestFuzzyScore << "\n";
		std::cout << "second-best fuzzy score: " << result.secondBestFuzzyScore << "\n";
		std::cout << "fuzzy margin: " << (result.bestFuzzyScore - result.secondBestFuzzyScore) << "\n";
		if (!notes.empty())
			std::cout << "notes: " << notes << "\n";
	}
}

TEST_CASE("case01 real capture local light 1.004 vs 1.005")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto capture1004 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.004/package.json");
	const auto capture1005 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.005/package.json");
	const auto package = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.005/package.json");

	const auto report = CompareCaptures(
		"Case 1: real capture, Local Light, 1.004 vs 1.005",
		capture1004,
		capture1005,
		{ package },
		ReplacementsFromPackage(package),
		capture1004.byteLength,
		capture1004.analysis,
		capture1004.rawHash,
		KnownTargetLengths(package),
		"Real ModifiedShader packages captured on each game patch. "
		"Candidate is the 1.004 original-game fingerprint; package is the 1.005 install.");

	CHECK_FALSE(report.rawHashEqual);
	CHECK(report.portableReflectionEqual);
	CHECK(report.semanticInstructionSetEqual);
	CHECK(report.crossVersionIdentityEqual);
	CHECK(report.modifiedShaderAccept);
	CHECK(report.replacementAccept);
	CHECK(report.enqueue15Percent);
	CHECK(report.replacementLengthGate5Percent);
}

TEST_CASE("case01 real capture directional light 1.004 vs 1.005")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto capture1004 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/directional-light/v1.004/package.json");
	const auto capture1005 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/directional-light/v1.005/package.json");
	const auto package = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/directional-light/v1.005/package.json");

	const auto report = CompareCaptures(
		"Case 1: real capture, Directional Light, 1.004 vs 1.005",
		capture1004,
		capture1005,
		{ package },
		ReplacementsFromPackage(package),
		capture1004.byteLength,
		capture1004.analysis,
		capture1004.rawHash,
		KnownTargetLengths(package),
		"Real ModifiedShader packages captured on each game patch. "
		"Candidate is the 1.004 original-game fingerprint; package is the 1.005 install.");

	CHECK_FALSE(report.rawHashEqual);
	CHECK(report.portableReflectionEqual);
	CHECK(report.semanticInstructionSetEqual);
	CHECK(report.crossVersionIdentityEqual);
	CHECK(report.modifiedShaderAccept);
	CHECK(report.replacementAccept);
	CHECK(report.enqueue15Percent);
	CHECK(report.replacementLengthGate5Percent);
}

TEST_CASE("case02 ambiguity multi-candidate local light vs directional light")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto candidate = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.004/package.json");
	const auto localLightPackage = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.005/package.json");
	const auto directionalLightPackage = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/directional-light/v1.005/package.json");

	const auto report = EvaluateAmbiguity(
		"Case 2: ambiguity baseline — 1.004 Local Light candidate vs two different 1.005 packages",
		{
			{ "local-light v1.005 (correct shader)", localLightPackage },
			{ "directional-light v1.005 (different shader)", directionalLightPackage },
		},
		candidate.byteLength,
		candidate.analysis,
		candidate.rawHash,
		"Fully real data. Simulates a typical install with multiple enabled ModifiedShader packages. "
		"The correct Local Light package should win clearly; Directional Light is a different pass.");

	CHECK(report.bestScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
	CHECK(report.clearsAmbiguityMargin);
	CHECK(report.modifiedShaderAccept);
	CHECK(report.winningPackageIndex == 0);
}

TEST_CASE("case02 ambiguity dual-version same shader packages")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto candidate = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.004/package.json");
	const auto package1004 = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.004/package.json");
	const auto package1005 = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.005/package.json");

	const auto report = EvaluateAmbiguity(
		"Case 2: ambiguity probe — 1.004 candidate vs both Local Light package versions enabled",
		{
			{ "local-light v1.004 (same patch)", package1004 },
			{ "local-light v1.005 (cross-patch)", package1005 },
		},
		candidate.byteLength,
		candidate.analysis,
		candidate.rawHash,
		"Fully real data. Models a player who kept a 1.004-authored package after upgrading "
		"and also created a 1.005 package for the same logical shader.");

	// Document observed behavior: same-patch package should outscore cross-patch sibling.
	CHECK(report.packageScores.size() == 2);
	CHECK(report.bestScore > report.secondBestScore);
	if (report.clearsAmbiguityMargin)
	{
		CHECK(report.modifiedShaderAccept);
		MESSAGE("Dual-version real packages clear the ambiguity margin; discovery accepts the same-patch winner.");
	}
	else
	{
		CHECK_FALSE(report.modifiedShaderAccept);
		MESSAGE("Dual-version real packages fall inside the ambiguity margin; discovery rejects despite a semantic winner.");
	}
}

TEST_CASE("case02 ambiguity constructed near-tie on real local light data")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto candidate = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.004/package.json");
	const auto correctPackage = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.005/package.json");
	const auto decoyPackage = MakeConstructedNearTieDecoy(correctPackage);

	const auto report = EvaluateAmbiguity(
		"Case 2: ambiguity near-tie — correct 1.005 Local Light vs constructed decoy clone",
		{
			{ "local-light v1.005 (correct)", correctPackage },
			{ "local-light v1.005 decoy (constructed)", decoyPackage },
		},
		candidate.byteLength,
		candidate.analysis,
		candidate.rawHash,
		"CONSTRUCTED SCENARIO on real 1.005 Local Light package data. "
		"The decoy is an exact in-memory clone (different id/name only) simulating "
		"two enabled packages that fingerprint identically against the runtime shader.");

	CHECK(report.bestScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
	CHECK(report.secondBestScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
	CHECK(report.margin < SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN);
	CHECK_FALSE(report.modifiedShaderAccept);
	MESSAGE("Constructed near-tie demonstrates ambiguity rejection: both packages score above the minimum "
		"but within 0.02 of each other, so DiscoverEnabledModifiedShader gives up.");
}

TEST_CASE("case03 replacement fuzzy fallback local light identity drift")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto capture1004 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.004/package.json");
	const auto capture1005 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.005/package.json");
	const auto package1005 = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.005/package.json");

	// Real 1.004 vs 1.005 share crossVersionIdentityHash (Case 1 exact path). To exercise
	// fuzzy fallback we flip crossVersion on an otherwise-identical 1.005 fingerprint —
	// models compile drift that changes identity hash while reflection stays aligned.
	const ShaderAnalysis::ShaderAnalysisDisk driftedCandidate =
		SimulateCrossVersionIdentityDrift(capture1005.analysis);
	const std::vector<ShaderTarget::ShaderTargetDisk> replacements = ReplacementsFromPackage(package1005);

	CHECK(capture1004.analysis.portableReflectionIdentityHash ==
		capture1005.analysis.portableReflectionIdentityHash);
	CHECK(capture1004.analysis.crossVersionIdentityHash == capture1005.analysis.crossVersionIdentityHash);
	CHECK(driftedCandidate.crossVersionIdentityHash != capture1005.analysis.crossVersionIdentityHash);
	CHECK(driftedCandidate.portableReflectionIdentityHash ==
		capture1005.analysis.portableReflectionIdentityHash);

	const auto fuzzyResult = CrossVersionDiscoveryTest::EvaluateReplacementDiscovery(
		capture1005.rawHash,
		ShaderTarget::PixelShader,
		capture1005.byteLength,
		driftedCandidate,
		replacements);
	PrintReplacementFuzzyReport(
		"Case 3: replacement fuzzy fallback — drifted crossVersionIdentityHash",
		fuzzyResult,
		"Real 1.005 Local Light fingerprint with a synthetic crossVersionIdentityHash flip. "
		"Portable reflection still matches; exact identity fails but similarity clears 0.90.");

	CHECK_FALSE(fuzzyResult.matchedByExactIdentity);
	CHECK(fuzzyResult.matchedByFuzzySimilarity);
	CHECK(fuzzyResult.accepted);
	CHECK(fuzzyResult.replacementIndex == 0);
	CHECK(fuzzyResult.bestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
	CHECK(fuzzyResult.bestFuzzyScore - fuzzyResult.secondBestFuzzyScore >=
		SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN);

	// Cross-patch 1.004 candidate against 1.005 replacement with only crossVersion drift
	// scores ~0.846 (below 0.90) because other compile-unstable fields already differ.
	const ShaderAnalysis::ShaderAnalysisDisk crossPatchDrifted =
		SimulateCrossVersionIdentityDrift(capture1004.analysis);
	const auto crossPatchResult = CrossVersionDiscoveryTest::EvaluateReplacementDiscovery(
		capture1004.rawHash,
		ShaderTarget::PixelShader,
		capture1004.byteLength,
		crossPatchDrifted,
		replacements);
	CHECK_FALSE(crossPatchResult.matchedByExactIdentity);
	CHECK_FALSE(crossPatchResult.accepted);
	CHECK(crossPatchResult.bestFuzzyScore < SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
}

TEST_CASE("case03 replacement fuzzy reflection gate blocks directional light")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto capture1005 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.005/package.json");
	const auto localLightPackage = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.005/package.json");
	const auto directionalLightPackage = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/directional-light/v1.005/package.json");

	const ShaderAnalysis::ShaderAnalysisDisk driftedCandidate =
		SimulateCrossVersionIdentityDrift(capture1005.analysis);

	std::vector<ShaderTarget::ShaderTargetDisk> replacements;
	for (const ModifiedShader::TargetDisk& target : localLightPackage.targets)
	{
		replacements.push_back(CrossVersionDiscoveryTest::MakeReplacementFromPackageTarget(
			target,
			localLightPackage.shaderType,
			localLightPackage.name));
	}
	for (const ModifiedShader::TargetDisk& target : directionalLightPackage.targets)
	{
		replacements.push_back(CrossVersionDiscoveryTest::MakeReplacementFromPackageTarget(
			target,
			directionalLightPackage.shaderType,
			directionalLightPackage.name + "_wrong"));
	}

	CHECK(replacements.size() == 2);
	CHECK(replacements[0].originalShaderAnalysis.portableReflectionIdentityHash !=
		replacements[1].originalShaderAnalysis.portableReflectionIdentityHash);

	const auto result = CrossVersionDiscoveryTest::EvaluateReplacementDiscovery(
		capture1005.rawHash,
		ShaderTarget::PixelShader,
		capture1005.byteLength,
		driftedCandidate,
		replacements);
	PrintReplacementFuzzyReport(
		"Case 3: replacement fuzzy reflection gate — Local Light vs Directional Light",
		result,
		"Both replacements are enabled, but only Local Light shares portableReflectionIdentityHash "
		"with the drifted candidate. Directional Light must never enter the fuzzy candidate set.");

	CHECK(result.accepted);
	CHECK(result.matchedByFuzzySimilarity);
	CHECK(result.replacementIndex == 0);
	CHECK(result.bestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
}

TEST_CASE("case03 replacement fuzzy ambiguity rejects dual local light targets")
{
	const auto root = CrossVersionDiscoveryTest::DataRoot();
	const auto capture1005 = CrossVersionDiscoveryTest::LoadPackageTargetCapture(
		root / "case01-real-captures/local-light/v1.005/package.json");
	const auto package1005 = CrossVersionDiscoveryTest::LoadPackageJson(
		root / "case01-real-captures/local-light/v1.005/package.json");

	const ShaderAnalysis::ShaderAnalysisDisk driftedCandidate =
		SimulateCrossVersionIdentityDrift(capture1005.analysis);

	std::vector<ShaderTarget::ShaderTargetDisk> replacements;
	replacements.push_back(CrossVersionDiscoveryTest::MakeReplacementFromPackageTarget(
		package1005.targets.front(),
		package1005.shaderType,
		package1005.name + " A"));
	// Exact clone — same analysis fingerprint, different ShaderTarget name.
	replacements.push_back(CrossVersionDiscoveryTest::MakeReplacementFromPackageTarget(
		package1005.targets.front(),
		package1005.shaderType,
		package1005.name + " B"));

	const auto result = CrossVersionDiscoveryTest::EvaluateReplacementDiscovery(
		capture1005.rawHash,
		ShaderTarget::PixelShader,
		capture1005.byteLength,
		driftedCandidate,
		replacements);
	PrintReplacementFuzzyReport(
		"Case 3: replacement fuzzy ambiguity — dual Local Light ShaderTargets",
		result,
		"Two enabled replacements share the same portable reflection gate and score identically "
		"against the drifted candidate; fuzzy matching should reject rather than guess.");

	CHECK(result.bestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
	CHECK(result.secondBestFuzzyScore >= SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE);
	CHECK(result.bestFuzzyScore - result.secondBestFuzzyScore <
		SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN);
	CHECK_FALSE(result.accepted);
	CHECK_FALSE(result.matchedByFuzzySimilarity);
}
