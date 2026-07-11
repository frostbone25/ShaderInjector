#include "SimilarityDiagnostics.h"

#include "SimilarityScore.h"

namespace CrossVersionDiscoveryTest
{
	std::vector<std::string> SimilarityDiagnostics::lowScoringFields(double threshold) const
	{
		std::vector<std::string> hurtFields;
		for (const SimilarityFieldScore& field : fields)
		{
			if (field.weight > 0.0 && field.score < threshold)
				hurtFields.push_back(field.name + "=" + std::to_string(field.score));
		}
		return hurtFields;
	}

	SimilarityDiagnostics AnalyzeShaderSimilarity(
		const ShaderAnalysis::ShaderAnalysisDisk& left,
		const ShaderAnalysis::ShaderAnalysisDisk& right)
	{
		SimilarityDiagnostics diagnostics{};

		if (!left.succeeded || !right.succeeded)
		{
			diagnostics.totalScore = SimilarityScore::Exact(left.succeeded, right.succeeded);
			diagnostics.fields.push_back({ "succeeded", diagnostics.totalScore, 1.0 });
			return diagnostics;
		}

		auto add = [&](const char* name, double score, double weight)
		{
			diagnostics.fields.push_back({ name, score, weight });
		};

		SimilarityScore::WeightedAverage score;
		add("shaderStage", SimilarityScore::Exact(left.shaderStage, right.shaderStage), 10.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("shaderModelMajor", SimilarityScore::Exact(left.shaderModelMajor, right.shaderModelMajor), 4.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("shaderModelMinor", SimilarityScore::Exact(left.shaderModelMinor, right.shaderModelMinor), 2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("compilationFlags", SimilarityScore::BitFlags(left.compilationFlags, right.compilationFlags), 1.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"containerParts",
			ShaderAnalysis::ContainerPartDisk::CalculateSimilarityScore(left.containerParts, right.containerParts),
			2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"inputParameters",
			ShaderAnalysis::SignatureParameterDisk::CalculateSimilarityScore(left.inputParameters, right.inputParameters),
			6.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"outputParameters",
			ShaderAnalysis::SignatureParameterDisk::CalculateSimilarityScore(left.outputParameters, right.outputParameters),
			6.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"patchConstantParameters",
			ShaderAnalysis::SignatureParameterDisk::CalculateSimilarityScore(left.patchConstantParameters, right.patchConstantParameters),
			3.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"resourceBindings",
			ShaderAnalysis::ResourceBindingDisk::CalculateSimilarityScore(left.resourceBindings, right.resourceBindings),
			8.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"constantBuffers",
			ShaderAnalysis::ConstantBufferDisk::CalculateSimilarityScore(left.constantBuffers, right.constantBuffers),
			8.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("instructionStatistics", left.instructionStatistics.CalculateSimilarityScore(right.instructionStatistics), 6.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("executionProperties", left.executionProperties.CalculateSimilarityScore(right.executionProperties), 6.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("interfaceSignatureHash", SimilarityScore::Exact(left.interfaceSignatureHash, right.interfaceSignatureHash), 2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("resourceSignatureHash", SimilarityScore::Exact(left.resourceSignatureHash, right.resourceSignatureHash), 2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("constantBufferSignatureHash", SimilarityScore::Exact(left.constantBufferSignatureHash, right.constantBufferSignatureHash), 2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("instructionStatisticsHash", SimilarityScore::Exact(left.instructionStatisticsHash, right.instructionStatisticsHash), 2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("executionSignatureHash", SimilarityScore::Exact(left.executionSignatureHash, right.executionSignatureHash), 2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"portableReflectionIdentityHash",
			SimilarityScore::Exact(left.portableReflectionIdentityHash, right.portableReflectionIdentityHash),
			4.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add("entryFunctionName", SimilarityScore::Exact(left.entryFunctionName, right.entryFunctionName), 1.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"normalizedInstructionCount",
			SimilarityScore::Numeric(left.normalizedInstructionCount, right.normalizedInstructionCount),
			2.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"semanticInstructionSetHash",
			SimilarityScore::Exact(left.semanticInstructionSetHash, right.semanticInstructionSetHash),
			8.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"crossVersionIdentityHash",
			SimilarityScore::Exact(left.crossVersionIdentityHash, right.crossVersionIdentityHash),
			10.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);
		add(
			"reflectionSignatureHash",
			SimilarityScore::Exact(left.reflectionSignatureHash, right.reflectionSignatureHash),
			3.0);
		score.Add(diagnostics.fields.back().score, diagnostics.fields.back().weight);

		diagnostics.totalScore = score.Result();
		return diagnostics;
	}
}
