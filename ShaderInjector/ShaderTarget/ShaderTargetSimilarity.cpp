#include "ShaderTarget.h"

#include "SimilarityScore.h"

namespace ShaderTarget
{
	namespace
	{
		#define DEFINE_COLLECTION_SCORE(Type) \
			double Type::CalculateSimilarityScore(const std::vector<Type>& left, const std::vector<Type>& right) \
			{ \
				return SimilarityScore::CalculateCollectionSimilarityScore(left, right); \
			}

		void AddStageIdentityScores(
			SimilarityScore::WeightedAverage& score,
			const std::string& leftHash,
			const std::string& rightHash,
			const std::string& leftLength,
			const std::string& rightLength)
		{
			score.Add(SimilarityScore::Exact(leftHash, rightHash), 1.0);
			score.Add(SimilarityScore::NumericString(leftLength, rightLength), 1.5);
		}
	}

	double ShaderPipelineTemplateDisk::CalculateSimilarityScore(const ShaderPipelineTemplateDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(sourceList, other.sourceList), 3.0);
		score.Add(SimilarityScore::Exact(pipelineCachedBlobHash, other.pipelineCachedBlobHash), 0.5);
		score.Add(SimilarityScore::NumericString(pipelineCachedBlobLength, other.pipelineCachedBlobLength));
		score.Add(SimilarityScore::Exact(rootSignatureHash, other.rootSignatureHash), 5.0);
		score.Add(SimilarityScore::NumericString(rootSignatureLength, other.rootSignatureLength), 2.0);
		AddStageIdentityScores(score, vsHash, other.vsHash, vsLength, other.vsLength);
		AddStageIdentityScores(score, psHash, other.psHash, psLength, other.psLength);
		AddStageIdentityScores(score, csHash, other.csHash, csLength, other.csLength);
		AddStageIdentityScores(score, gsHash, other.gsHash, gsLength, other.gsLength);
		AddStageIdentityScores(score, hsHash, other.hsHash, hsLength, other.hsLength);
		AddStageIdentityScores(score, dsHash, other.dsHash, dsLength, other.dsLength);
		score.Add(SimilarityScore::NumericString(pipelineStreamLength, other.pipelineStreamLength), 2.0);
		score.Add(SimilarityScore::Exact(pipelineStreamSubobjectTypes, other.pipelineStreamSubobjectTypes), 4.0);
		score.Add(SimilarityScore::NumericString(inputLayoutElementCount, other.inputLayoutElementCount), 2.0);
		score.Add(SimilarityScore::Exact(inputLayoutSignature, other.inputLayoutSignature), 6.0);
		score.Add(SimilarityScore::NumericString(streamOutputDeclarationCount, other.streamOutputDeclarationCount), 2.0);
		score.Add(SimilarityScore::Exact(streamOutputSignature, other.streamOutputSignature), 4.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ShaderPipelineTemplateDisk)

	double ShaderTargetDisk::CalculateSimilarityScore(const ShaderTargetDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(shaderType, other.shaderType), 12.0);
		score.Add(SimilarityScore::Exact(shaderProfile, other.shaderProfile), 3.0);
		score.Add(SimilarityScore::Exact(shaderEntryPoint, other.shaderEntryPoint));
		score.Add(SimilarityScore::Exact(originalShaderBytecodeHash, other.originalShaderBytecodeHash), 3.0);
		score.Add(SimilarityScore::NumericString(originalShaderBytecodeLength, other.originalShaderBytecodeLength), 3.0);

		if (originalShaderAnalysis.succeeded || other.originalShaderAnalysis.succeeded)
			score.Add(originalShaderAnalysis.CalculateSimilarityScore(other.originalShaderAnalysis), 20.0);

		score.Add(SimilarityScore::Exact(sourceList, other.sourceList), 2.0);
		score.Add(SimilarityScore::Exact(pipelineStateType, other.pipelineStateType), 4.0);
		score.Add(SimilarityScore::Exact(targetSubobjectType, other.targetSubobjectType), 5.0);
		score.Add(SimilarityScore::Exact(pipelineCachedBlobHash, other.pipelineCachedBlobHash), 0.5);
		score.Add(SimilarityScore::NumericString(pipelineCachedBlobLength, other.pipelineCachedBlobLength));
		score.Add(SimilarityScore::Exact(rootSignatureHash, other.rootSignatureHash), 5.0);
		score.Add(SimilarityScore::NumericString(rootSignatureLength, other.rootSignatureLength), 2.0);

		AddStageIdentityScores(score, vsHash, other.vsHash, vsLength, other.vsLength);
		AddStageIdentityScores(score, psHash, other.psHash, psLength, other.psLength);
		AddStageIdentityScores(score, csHash, other.csHash, csLength, other.csLength);
		AddStageIdentityScores(score, gsHash, other.gsHash, gsLength, other.gsLength);
		AddStageIdentityScores(score, hsHash, other.hsHash, hsLength, other.hsLength);
		AddStageIdentityScores(score, dsHash, other.dsHash, dsLength, other.dsLength);

		score.Add(SimilarityScore::Exact(renderTargetFormat0, other.renderTargetFormat0), 3.0);
		score.Add(SimilarityScore::Exact(renderTargetFormats, other.renderTargetFormats), 4.0);
		score.Add(SimilarityScore::NumericString(numRenderTargets, other.numRenderTargets), 2.0);
		score.Add(SimilarityScore::Exact(depthStencilFormat, other.depthStencilFormat), 4.0);
		score.Add(SimilarityScore::Exact(primitiveTopologyType, other.primitiveTopologyType), 3.0);
		score.Add(SimilarityScore::NumericString(sampleCount, other.sampleCount), 2.0);
		score.Add(SimilarityScore::NumericString(sampleQuality, other.sampleQuality));
		score.Add(SimilarityScore::Exact(sampleMask, other.sampleMask), 2.0);
		score.Add(SimilarityScore::Exact(blendStateHash, other.blendStateHash), 3.0);
		score.Add(SimilarityScore::Exact(rasterizerStateHash, other.rasterizerStateHash), 3.0);
		score.Add(SimilarityScore::Exact(depthStencilStateHash, other.depthStencilStateHash), 3.0);
		score.Add(SimilarityScore::NumericString(pipelineStreamLength, other.pipelineStreamLength), 2.0);
		score.Add(SimilarityScore::Exact(pipelineStreamSubobjectTypes, other.pipelineStreamSubobjectTypes), 4.0);
		score.Add(SimilarityScore::NumericString(inputLayoutElementCount, other.inputLayoutElementCount), 2.0);
		score.Add(SimilarityScore::Exact(inputLayoutSignature, other.inputLayoutSignature), 6.0);
		score.Add(SimilarityScore::NumericString(streamOutputDeclarationCount, other.streamOutputDeclarationCount), 2.0);
		score.Add(SimilarityScore::Exact(streamOutputSignature, other.streamOutputSignature), 4.0);
		score.Add(ShaderPipelineTemplateDisk::CalculateSimilarityScore(pipelineTemplates, other.pipelineTemplates), 10.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ShaderTargetDisk)

	double ShaderInputElementDisk::CalculateSimilarityScore(const ShaderInputElementDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(semanticName, other.semanticName), 5.0);
		score.Add(SimilarityScore::Exact(semanticIndex, other.semanticIndex), 3.0);
		score.Add(SimilarityScore::Exact(format, other.format), 4.0);
		score.Add(SimilarityScore::Exact(inputSlot, other.inputSlot), 3.0);
		score.Add(SimilarityScore::Exact(alignedByteOffset, other.alignedByteOffset), 3.0);
		score.Add(SimilarityScore::Exact(inputSlotClass, other.inputSlotClass), 2.0);
		score.Add(SimilarityScore::Exact(instanceDataStepRate, other.instanceDataStepRate), 2.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ShaderInputElementDisk)

	double ShaderStreamOutputDeclarationDisk::CalculateSimilarityScore(const ShaderStreamOutputDeclarationDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(semanticName, other.semanticName), 5.0);
		score.Add(SimilarityScore::Exact(semanticIndex, other.semanticIndex), 3.0);
		score.Add(SimilarityScore::Exact(startComponent, other.startComponent), 3.0);
		score.Add(SimilarityScore::Exact(componentCount, other.componentCount), 3.0);
		score.Add(SimilarityScore::Exact(outputSlot, other.outputSlot), 3.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ShaderStreamOutputDeclarationDisk)

	double ShaderPipelineStreamMetadataDisk::CalculateSimilarityScore(const ShaderPipelineStreamMetadataDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(ShaderInputElementDisk::CalculateSimilarityScore(inputElements, other.inputElements), 6.0);
		score.Add(ShaderStreamOutputDeclarationDisk::CalculateSimilarityScore(streamOutputDeclarations, other.streamOutputDeclarations), 5.0);
		score.Add(SimilarityScore::CalculateOrderedNumericCollectionSimilarityScore(streamOutputStrides, other.streamOutputStrides), 3.0);
		score.Add(SimilarityScore::Exact(hasViewInstancing, other.hasViewInstancing), 2.0);
		score.Add(SimilarityScore::Exact(viewInstancingFlags, other.viewInstancingFlags), 2.0);
		score.Add(SimilarityScore::CalculateOrderedNumericCollectionSimilarityScore(viewInstanceViewportArrayIndices, other.viewInstanceViewportArrayIndices), 3.0);
		score.Add(SimilarityScore::CalculateOrderedNumericCollectionSimilarityScore(viewInstanceRenderTargetArrayIndices, other.viewInstanceRenderTargetArrayIndices), 3.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ShaderPipelineStreamMetadataDisk)

	#undef DEFINE_COLLECTION_SCORE
}
