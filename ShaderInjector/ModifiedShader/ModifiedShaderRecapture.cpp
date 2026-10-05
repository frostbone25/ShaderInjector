#include "ModifiedShaderRecapture.h"

#include <algorithm>
#include <d3d12shader.h>

#include "Hash/Hash.h"

namespace ModifiedShaderRecapture
{
	bool MatchesGraphicsPipelineState(const ShaderTarget::ShaderTargetDisk& saved, const ShaderTarget::ShaderTargetDisk& captured)
	{
		const bool shaderStagesMatch = saved.vsHash == captured.vsHash && saved.psHash == captured.psHash && saved.gsHash == captured.gsHash && saved.hsHash == captured.hsHash && saved.dsHash == captured.dsHash;
		const bool layoutsMatch = saved.rootSignatureHash == captured.rootSignatureHash && saved.inputLayoutSignature == captured.inputLayoutSignature && saved.streamOutputSignature == captured.streamOutputSignature;
		const bool outputFormatsMatch = saved.renderTargetFormats == captured.renderTargetFormats && saved.numRenderTargets == captured.numRenderTargets && saved.depthStencilFormat == captured.depthStencilFormat;
		const bool samplesMatch = saved.sampleCount == captured.sampleCount && saved.sampleQuality == captured.sampleQuality && saved.sampleMask == captured.sampleMask && saved.primitiveTopologyType == captured.primitiveTopologyType;
		const bool fixedStatesMatch = saved.blendStateHash == captured.blendStateHash && saved.rasterizerStateHash == captured.rasterizerStateHash && saved.depthStencilStateHash == captured.depthStencilStateHash;
		return shaderStagesMatch && layoutsMatch && outputFormatsMatch && samplesMatch && fixedStatesMatch;
	}

	bool UpdateFingerprint(ModifiedShader::ModifiedShaderPackageDisk& package, uint64_t shaderHash, size_t bytecodeLength, const ShaderAnalysis::ShaderAnalysisDisk& analysis)
	{
		if (!shaderHash || !bytecodeLength || !analysis.succeeded)
			return false;
		const uint32_t shaderStages[] = {D3D12_SHVER_VERTEX_SHADER, D3D12_SHVER_HULL_SHADER, D3D12_SHVER_DOMAIN_SHADER, D3D12_SHVER_GEOMETRY_SHADER, D3D12_SHVER_PIXEL_SHADER, D3D12_SHVER_COMPUTE_SHADER, D3D12_SHVER_AMPLIFICATION_SHADER, D3D12_SHVER_MESH_SHADER};
		if (package.shaderType < ShaderTarget::VertexShader || package.shaderType >= ShaderTarget::Unknown || analysis.shaderStage != shaderStages[package.shaderType])
			return false;

		bool updatedExistingFingerprint = false;
		for (ModifiedShader::ModifiedShaderTargetDisk& target : package.targets)
		{
			const bool matchesHash = std::any_of(target.knownShaderBytecodeHashes.begin(), target.knownShaderBytecodeHashes.end(), [shaderHash](const std::string& knownHash)
			{
				return Hash::ParseHashText(knownHash) == shaderHash;
			});
			if (!matchesHash)
				continue;

			//start with the current structure so removed analysis fields disappear when the JSON is written again.
			target.shaderAnalysis = analysis;
			target.originalShaderBytecodeLength = std::to_string(bytecodeLength);
			updatedExistingFingerprint = true;
		}

		if (!updatedExistingFingerprint)
		{
			ModifiedShader::ModifiedShaderTargetDisk target{};
			target.name = "CapturedShader_" + Hash::FormatHash(shaderHash);
			target.knownShaderBytecodeHashes.push_back(Hash::FormatHash(shaderHash));
			target.originalShaderBytecodeLength = std::to_string(bytecodeLength);
			target.shaderAnalysis = analysis;
			package.targets.push_back(std::move(target));
		}

		return true;
	}
}
