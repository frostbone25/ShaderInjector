#include "ShaderAnalysis.h"

#include "SimilarityScore.h"

namespace ShaderAnalysis
{
	namespace
	{
#define DEFINE_COLLECTION_SCORE(Type)                                                                    \
	double Type::CalculateSimilarityScore(const std::vector<Type>& left, const std::vector<Type>& right) \
	{                                                                                                    \
		return SimilarityScore::CalculateCollectionSimilarityScore(left, right);                         \
	}
	} //namespace

	double ContainerPartDisk::CalculateSimilarityScore(const ContainerPartDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(kind, other.kind), 5.0);
		score.Add(SimilarityScore::Numeric(size, other.size), 2.0);
		score.Add(SimilarityScore::Exact(contentHash, other.contentHash), 1.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ContainerPartDisk)

	double SignatureParameterDisk::CalculateSimilarityScore(const SignatureParameterDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(semanticName, other.semanticName), 4.0);
		score.Add(SimilarityScore::Exact(semanticIndex, other.semanticIndex), 3.0);
		score.Add(SimilarityScore::Exact(registerIndex, other.registerIndex), 3.0);
		score.Add(SimilarityScore::Exact(systemValueType, other.systemValueType), 2.0);
		score.Add(SimilarityScore::Exact(componentType, other.componentType), 2.0);
		score.Add(SimilarityScore::BitFlags(mask, other.mask), 2.0);
		score.Add(SimilarityScore::BitFlags(readWriteMask, other.readWriteMask), 2.0);
		score.Add(SimilarityScore::Exact(stream, other.stream), 1.0);
		score.Add(SimilarityScore::Exact(minimumPrecision, other.minimumPrecision), 1.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(SignatureParameterDisk)

	double ResourceBindingDisk::CalculateSimilarityScore(const ResourceBindingDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(type, other.type), 5.0);
		score.Add(SimilarityScore::Exact(bindPoint, other.bindPoint), 4.0);
		score.Add(SimilarityScore::Exact(bindCount, other.bindCount), 3.0);
		score.Add(SimilarityScore::BitFlags(flags, other.flags), 1.0);
		score.Add(SimilarityScore::Exact(returnType, other.returnType), 2.0);
		score.Add(SimilarityScore::Exact(dimension, other.dimension), 3.0);
		score.Add(SimilarityScore::Numeric(sampleCountOrStride, other.sampleCountOrStride), 2.0);
		score.Add(SimilarityScore::Exact(registerSpace, other.registerSpace), 5.0);
		score.Add(SimilarityScore::Exact(rangeId, other.rangeId), 0.5);
		score.Add(SimilarityScore::Exact(name, other.name), 0.25);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ResourceBindingDisk)

	double TypeLayoutDisk::CalculateSimilarityScore(const TypeLayoutDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(variableClass, other.variableClass), 4.0);
		score.Add(SimilarityScore::Exact(variableType, other.variableType), 4.0);
		score.Add(SimilarityScore::Exact(rows, other.rows), 2.0);
		score.Add(SimilarityScore::Exact(columns, other.columns), 2.0);
		score.Add(SimilarityScore::Exact(elements, other.elements), 2.0);
		score.Add(SimilarityScore::Exact(offset, other.offset), 2.0);
		score.Add(CalculateSimilarityScore(members, other.members), 5.0);
		score.Add(SimilarityScore::Exact(name, other.name), 0.25);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(TypeLayoutDisk)

	double ConstantBufferVariableDisk::CalculateSimilarityScore(const ConstantBufferVariableDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(startOffset, other.startOffset), 4.0);
		score.Add(SimilarityScore::Exact(size, other.size), 4.0);
		score.Add(SimilarityScore::BitFlags(flags, other.flags), 1.0);
		score.Add(SimilarityScore::Exact(startTexture, other.startTexture), 1.0);
		score.Add(SimilarityScore::Exact(textureCount, other.textureCount), 1.0);
		score.Add(SimilarityScore::Exact(startSampler, other.startSampler), 1.0);
		score.Add(SimilarityScore::Exact(samplerCount, other.samplerCount), 1.0);
		score.Add(typeLayout.CalculateSimilarityScore(other.typeLayout), 6.0);
		score.Add(SimilarityScore::Exact(name, other.name), 0.25);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ConstantBufferVariableDisk)

	double ConstantBufferDisk::CalculateSimilarityScore(const ConstantBufferDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(type, other.type), 3.0);
		score.Add(SimilarityScore::Exact(size, other.size), 4.0);
		score.Add(SimilarityScore::BitFlags(flags, other.flags), 1.0);
		score.Add(ConstantBufferVariableDisk::CalculateSimilarityScore(variables, other.variables), 8.0);
		score.Add(SimilarityScore::Exact(name, other.name), 0.25);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ConstantBufferDisk)

	double InstructionStatisticsDisk::CalculateSimilarityScore(const InstructionStatisticsDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Numeric(instructionCount, other.instructionCount), 4.0);
		score.Add(SimilarityScore::Numeric(temporaryRegisterCount, other.temporaryRegisterCount));
		score.Add(SimilarityScore::Numeric(temporaryArrayCount, other.temporaryArrayCount));
		score.Add(SimilarityScore::Numeric(constantDefinitionCount, other.constantDefinitionCount));
		score.Add(SimilarityScore::Numeric(declarationCount, other.declarationCount));
		score.Add(SimilarityScore::Numeric(textureNormalInstructionCount, other.textureNormalInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(textureLoadInstructionCount, other.textureLoadInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(textureComparisonInstructionCount, other.textureComparisonInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(textureBiasInstructionCount, other.textureBiasInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(textureGradientInstructionCount, other.textureGradientInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(floatInstructionCount, other.floatInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(signedIntegerInstructionCount, other.signedIntegerInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(unsignedIntegerInstructionCount, other.unsignedIntegerInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(staticFlowControlCount, other.staticFlowControlCount), 2.0);
		score.Add(SimilarityScore::Numeric(dynamicFlowControlCount, other.dynamicFlowControlCount), 2.0);
		score.Add(SimilarityScore::Numeric(macroInstructionCount, other.macroInstructionCount));
		score.Add(SimilarityScore::Numeric(arrayInstructionCount, other.arrayInstructionCount));
		score.Add(SimilarityScore::Numeric(cutInstructionCount, other.cutInstructionCount));
		score.Add(SimilarityScore::Numeric(emitInstructionCount, other.emitInstructionCount));
		score.Add(SimilarityScore::Numeric(barrierInstructionCount, other.barrierInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(interlockedInstructionCount, other.interlockedInstructionCount), 2.0);
		score.Add(SimilarityScore::Numeric(textureStoreInstructionCount, other.textureStoreInstructionCount), 2.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(InstructionStatisticsDisk)

	double ExecutionPropertiesDisk::CalculateSimilarityScore(const ExecutionPropertiesDisk& other) const
	{
		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(geometryOutputTopology, other.geometryOutputTopology), 2.0);
		score.Add(SimilarityScore::Numeric(geometryMaximumOutputVertexCount, other.geometryMaximumOutputVertexCount), 2.0);
		score.Add(SimilarityScore::Exact(inputPrimitive, other.inputPrimitive), 2.0);
		score.Add(SimilarityScore::Numeric(geometryInstanceCount, other.geometryInstanceCount));
		score.Add(SimilarityScore::Exact(controlPointCount, other.controlPointCount), 2.0);
		score.Add(SimilarityScore::Exact(hullOutputPrimitive, other.hullOutputPrimitive), 2.0);
		score.Add(SimilarityScore::Exact(hullPartitioning, other.hullPartitioning), 2.0);
		score.Add(SimilarityScore::Exact(tessellatorDomain, other.tessellatorDomain), 2.0);
		score.Add(SimilarityScore::Exact(threadGroupSizeX, other.threadGroupSizeX), 3.0);
		score.Add(SimilarityScore::Exact(threadGroupSizeY, other.threadGroupSizeY), 3.0);
		score.Add(SimilarityScore::Exact(threadGroupSizeZ, other.threadGroupSizeZ), 3.0);
		score.Add(SimilarityScore::Numeric(threadGroupTotalSize, other.threadGroupTotalSize), 2.0);
		score.Add(SimilarityScore::Exact(minimumFeatureLevel, other.minimumFeatureLevel), 2.0);
		score.Add(SimilarityScore::BitFlags(requiresFlags, other.requiresFlags), 4.0);
		score.Add(SimilarityScore::Exact(sampleFrequencyShader, other.sampleFrequencyShader), 2.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ExecutionPropertiesDisk)

	double ShaderAnalysisDisk::CalculateSimilarityScore(const ShaderAnalysisDisk& other) const
	{
		if (!succeeded || !other.succeeded)
			return SimilarityScore::Exact(succeeded, other.succeeded);

		SimilarityScore::WeightedAverage score;
		score.Add(SimilarityScore::Exact(shaderStage, other.shaderStage), 10.0);
		score.Add(SimilarityScore::Exact(shaderModelMajor, other.shaderModelMajor), 4.0);
		score.Add(SimilarityScore::Exact(shaderModelMinor, other.shaderModelMinor), 2.0);
		score.Add(SimilarityScore::BitFlags(compilationFlags, other.compilationFlags), 1.0);
		score.Add(ContainerPartDisk::CalculateSimilarityScore(containerParts, other.containerParts), 2.0);
		score.Add(SignatureParameterDisk::CalculateSimilarityScore(inputParameters, other.inputParameters), 6.0);
		score.Add(SignatureParameterDisk::CalculateSimilarityScore(outputParameters, other.outputParameters), 6.0);
		score.Add(SignatureParameterDisk::CalculateSimilarityScore(patchConstantParameters, other.patchConstantParameters), 3.0);
		score.Add(ResourceBindingDisk::CalculateSimilarityScore(resourceBindings, other.resourceBindings), 8.0);
		score.Add(ConstantBufferDisk::CalculateSimilarityScore(constantBuffers, other.constantBuffers), 8.0);
		score.Add(instructionStatistics.CalculateSimilarityScore(other.instructionStatistics), 6.0);
		score.Add(executionProperties.CalculateSimilarityScore(other.executionProperties), 6.0);
		score.Add(SimilarityScore::Exact(interfaceSignatureHash, other.interfaceSignatureHash), 2.0);
		score.Add(SimilarityScore::Exact(resourceSignatureHash, other.resourceSignatureHash), 2.0);
		score.Add(SimilarityScore::Exact(constantBufferSignatureHash, other.constantBufferSignatureHash), 2.0);
		score.Add(SimilarityScore::Exact(instructionStatisticsHash, other.instructionStatisticsHash), 2.0);
		score.Add(SimilarityScore::Exact(executionSignatureHash, other.executionSignatureHash), 2.0);
		score.Add(SimilarityScore::Exact(portableReflectionIdentityHash, other.portableReflectionIdentityHash), 4.0);
		score.Add(SimilarityScore::Exact(entryFunctionName, other.entryFunctionName), 1.0);
		score.Add(SimilarityScore::Numeric(normalizedInstructionCount, other.normalizedInstructionCount), 2.0);
		score.Add(SimilarityScore::Exact(semanticInstructionSetHash, other.semanticInstructionSetHash), 8.0);
		score.Add(SimilarityScore::Exact(crossVersionIdentityHash, other.crossVersionIdentityHash), 10.0);
		score.Add(SimilarityScore::Exact(reflectionSignatureHash, other.reflectionSignatureHash), 3.0);
		return score.Result();
	}
	DEFINE_COLLECTION_SCORE(ShaderAnalysisDisk)

#undef DEFINE_COLLECTION_SCORE
} //namespace ShaderAnalysis
