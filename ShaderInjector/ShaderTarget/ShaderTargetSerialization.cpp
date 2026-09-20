#include "ShaderTarget/ShaderTargetDisk.h"

namespace ShaderTarget
{
	template <typename T>
	void ReadJsonField(const nlohmann::ordered_json& json, const char* fieldName, T& value)
	{
		if (json.contains(fieldName))
			value = json[fieldName].get<T>();
	}

	void to_json(nlohmann::ordered_json& json, const ShaderTargetDisk& shaderTarget)
	{
		json = nlohmann::ordered_json{};

#define SHADER_TARGET_WRITE_FIELD(fieldName) json[#fieldName] = shaderTarget.fieldName
		SHADER_TARGET_WRITE_FIELD(schemaVersion);
		SHADER_TARGET_WRITE_FIELD(enabled);
		SHADER_TARGET_WRITE_FIELD(name);
		SHADER_TARGET_WRITE_FIELD(shaderType);
		SHADER_TARGET_WRITE_FIELD(shaderProfile);
		SHADER_TARGET_WRITE_FIELD(shaderEntryPoint);
		SHADER_TARGET_WRITE_FIELD(originalShaderBytecodeHash);
		SHADER_TARGET_WRITE_FIELD(shaderBytecodeHashAliases);
		SHADER_TARGET_WRITE_FIELD(originalShaderBytecodeLength);
		SHADER_TARGET_WRITE_FIELD(originalShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(originalShaderAnalysis);
		SHADER_TARGET_WRITE_FIELD(modifiedShaderId);
		SHADER_TARGET_WRITE_FIELD(replacementDirectory);
		SHADER_TARGET_WRITE_FIELD(jsonPath);
		SHADER_TARGET_WRITE_FIELD(sourceList);
		SHADER_TARGET_WRITE_FIELD(pipelineIndex);
		SHADER_TARGET_WRITE_FIELD(pipelineStateType);
		SHADER_TARGET_WRITE_FIELD(psoPointer);
		SHADER_TARGET_WRITE_FIELD(pipelineCachedBlobHash);
		SHADER_TARGET_WRITE_FIELD(pipelineCachedBlobHashAliases);
		SHADER_TARGET_WRITE_FIELD(pipelineCachedBlobLength);
		SHADER_TARGET_WRITE_FIELD(pipelineCachedBlobPath);
		SHADER_TARGET_WRITE_FIELD(pipelineStreamBlobPath);
		SHADER_TARGET_WRITE_FIELD(pipelineStreamMetadataPath);
		SHADER_TARGET_WRITE_FIELD(rootSignatureBlobPath);
		SHADER_TARGET_WRITE_FIELD(rootSignatureHash);
		SHADER_TARGET_WRITE_FIELD(vertexShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(pixelShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(computeShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(geometryShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(hullShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(domainShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(amplificationShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(meshShaderBlobPath);
		SHADER_TARGET_WRITE_FIELD(targetSubobjectType);
		SHADER_TARGET_WRITE_FIELD(vsHash);
		SHADER_TARGET_WRITE_FIELD(psHash);
		SHADER_TARGET_WRITE_FIELD(csHash);
		SHADER_TARGET_WRITE_FIELD(gsHash);
		SHADER_TARGET_WRITE_FIELD(hsHash);
		SHADER_TARGET_WRITE_FIELD(dsHash);
		SHADER_TARGET_WRITE_FIELD(asHash);
		SHADER_TARGET_WRITE_FIELD(msHash);
		SHADER_TARGET_WRITE_FIELD(vsLength);
		SHADER_TARGET_WRITE_FIELD(psLength);
		SHADER_TARGET_WRITE_FIELD(csLength);
		SHADER_TARGET_WRITE_FIELD(gsLength);
		SHADER_TARGET_WRITE_FIELD(hsLength);
		SHADER_TARGET_WRITE_FIELD(dsLength);
		SHADER_TARGET_WRITE_FIELD(asLength);
		SHADER_TARGET_WRITE_FIELD(msLength);
		SHADER_TARGET_WRITE_FIELD(renderTargetFormat0);
		SHADER_TARGET_WRITE_FIELD(renderTargetFormats);
		SHADER_TARGET_WRITE_FIELD(numRenderTargets);
		SHADER_TARGET_WRITE_FIELD(depthStencilFormat);
		SHADER_TARGET_WRITE_FIELD(primitiveTopologyType);
		SHADER_TARGET_WRITE_FIELD(sampleCount);
		SHADER_TARGET_WRITE_FIELD(sampleQuality);
		SHADER_TARGET_WRITE_FIELD(sampleMask);
		SHADER_TARGET_WRITE_FIELD(blendStateHash);
		SHADER_TARGET_WRITE_FIELD(rasterizerStateHash);
		SHADER_TARGET_WRITE_FIELD(depthStencilStateHash);
		SHADER_TARGET_WRITE_FIELD(pipelineFixedFunctionStateHash);
		SHADER_TARGET_WRITE_FIELD(pipelineStreamLength);
		SHADER_TARGET_WRITE_FIELD(pipelineStreamSubobjectTypes);
		SHADER_TARGET_WRITE_FIELD(rootSignatureLength);
		SHADER_TARGET_WRITE_FIELD(inputLayoutElementCount);
		SHADER_TARGET_WRITE_FIELD(inputLayoutSignature);
		SHADER_TARGET_WRITE_FIELD(streamOutputDeclarationCount);
		SHADER_TARGET_WRITE_FIELD(streamOutputSignature);
		SHADER_TARGET_WRITE_FIELD(pipelineTemplates);
#undef SHADER_TARGET_WRITE_FIELD
	}

	void from_json(const nlohmann::ordered_json& json, ShaderTargetDisk& shaderTarget)
	{
		shaderTarget = ShaderTargetDisk{};

#define SHADER_TARGET_READ_FIELD(fieldName) ReadJsonField(json, #fieldName, shaderTarget.fieldName)
		SHADER_TARGET_READ_FIELD(schemaVersion);
		SHADER_TARGET_READ_FIELD(enabled);
		SHADER_TARGET_READ_FIELD(name);
		SHADER_TARGET_READ_FIELD(shaderType);
		SHADER_TARGET_READ_FIELD(shaderProfile);
		SHADER_TARGET_READ_FIELD(shaderEntryPoint);
		SHADER_TARGET_READ_FIELD(originalShaderBytecodeHash);
		SHADER_TARGET_READ_FIELD(shaderBytecodeHashAliases);
		SHADER_TARGET_READ_FIELD(originalShaderBytecodeLength);
		SHADER_TARGET_READ_FIELD(originalShaderBlobPath);
		SHADER_TARGET_READ_FIELD(originalShaderAnalysis);
		SHADER_TARGET_READ_FIELD(modifiedShaderId);
		SHADER_TARGET_READ_FIELD(replacementDirectory);
		SHADER_TARGET_READ_FIELD(jsonPath);
		SHADER_TARGET_READ_FIELD(sourceList);
		SHADER_TARGET_READ_FIELD(pipelineIndex);
		SHADER_TARGET_READ_FIELD(pipelineStateType);
		SHADER_TARGET_READ_FIELD(psoPointer);
		SHADER_TARGET_READ_FIELD(pipelineCachedBlobHash);
		SHADER_TARGET_READ_FIELD(pipelineCachedBlobHashAliases);
		SHADER_TARGET_READ_FIELD(pipelineCachedBlobLength);
		SHADER_TARGET_READ_FIELD(pipelineCachedBlobPath);
		SHADER_TARGET_READ_FIELD(pipelineStreamBlobPath);
		SHADER_TARGET_READ_FIELD(pipelineStreamMetadataPath);
		SHADER_TARGET_READ_FIELD(rootSignatureBlobPath);
		SHADER_TARGET_READ_FIELD(rootSignatureHash);
		SHADER_TARGET_READ_FIELD(vertexShaderBlobPath);
		SHADER_TARGET_READ_FIELD(pixelShaderBlobPath);
		SHADER_TARGET_READ_FIELD(computeShaderBlobPath);
		SHADER_TARGET_READ_FIELD(geometryShaderBlobPath);
		SHADER_TARGET_READ_FIELD(hullShaderBlobPath);
		SHADER_TARGET_READ_FIELD(domainShaderBlobPath);
		SHADER_TARGET_READ_FIELD(amplificationShaderBlobPath);
		SHADER_TARGET_READ_FIELD(meshShaderBlobPath);
		SHADER_TARGET_READ_FIELD(targetSubobjectType);
		SHADER_TARGET_READ_FIELD(vsHash);
		SHADER_TARGET_READ_FIELD(psHash);
		SHADER_TARGET_READ_FIELD(csHash);
		SHADER_TARGET_READ_FIELD(gsHash);
		SHADER_TARGET_READ_FIELD(hsHash);
		SHADER_TARGET_READ_FIELD(dsHash);
		SHADER_TARGET_READ_FIELD(asHash);
		SHADER_TARGET_READ_FIELD(msHash);
		SHADER_TARGET_READ_FIELD(vsLength);
		SHADER_TARGET_READ_FIELD(psLength);
		SHADER_TARGET_READ_FIELD(csLength);
		SHADER_TARGET_READ_FIELD(gsLength);
		SHADER_TARGET_READ_FIELD(hsLength);
		SHADER_TARGET_READ_FIELD(dsLength);
		SHADER_TARGET_READ_FIELD(asLength);
		SHADER_TARGET_READ_FIELD(msLength);
		SHADER_TARGET_READ_FIELD(renderTargetFormat0);
		SHADER_TARGET_READ_FIELD(renderTargetFormats);
		SHADER_TARGET_READ_FIELD(numRenderTargets);
		SHADER_TARGET_READ_FIELD(depthStencilFormat);
		SHADER_TARGET_READ_FIELD(primitiveTopologyType);
		SHADER_TARGET_READ_FIELD(sampleCount);
		SHADER_TARGET_READ_FIELD(sampleQuality);
		SHADER_TARGET_READ_FIELD(sampleMask);
		SHADER_TARGET_READ_FIELD(blendStateHash);
		SHADER_TARGET_READ_FIELD(rasterizerStateHash);
		SHADER_TARGET_READ_FIELD(depthStencilStateHash);
		SHADER_TARGET_READ_FIELD(pipelineFixedFunctionStateHash);
		SHADER_TARGET_READ_FIELD(pipelineStreamLength);
		SHADER_TARGET_READ_FIELD(pipelineStreamSubobjectTypes);
		SHADER_TARGET_READ_FIELD(rootSignatureLength);
		SHADER_TARGET_READ_FIELD(inputLayoutElementCount);
		SHADER_TARGET_READ_FIELD(inputLayoutSignature);
		SHADER_TARGET_READ_FIELD(streamOutputDeclarationCount);
		SHADER_TARGET_READ_FIELD(streamOutputSignature);
		SHADER_TARGET_READ_FIELD(pipelineTemplates);
#undef SHADER_TARGET_READ_FIELD
	}
}
