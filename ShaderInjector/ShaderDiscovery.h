#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ShaderTarget.h"
#include "ModifiedShader.h"

#ifndef SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE
#define SHADER_INJECTOR_DISCOVERY_MINIMUM_SIMILARITY_SCORE 0.90
#endif

#ifndef SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN
#define SHADER_INJECTOR_DISCOVERY_SIMILARITY_AMBIGUITY_MARGIN 0.02
#endif

namespace ShaderDiscovery
{
	void ResetRuntimeCache();
	bool EnsureReplacementAnalysis(ShaderTarget::ShaderTargetDisk& replacement);
	bool PersistShaderHashAlias(ShaderTarget::ShaderTargetDisk& replacement, uint64_t shaderHash);
	int DiscoverEnabledReplacement(
		uint64_t shaderHash,
		ShaderTarget::ShaderType shaderType,
		const std::vector<uint8_t>& shaderBytecode,
		const std::vector<ShaderTarget::ShaderTargetDisk>& replacements);

	int DiscoverEnabledModifiedShader(
		uint64_t shaderHash,
		ShaderTarget::ShaderType shaderType,
		const std::vector<uint8_t>& shaderBytecode,
		const std::vector<ModifiedShader::PackageDisk>& modifiedShaders,
		ShaderAnalysis::ShaderAnalysisDisk* outCandidateAnalysis = nullptr);
}
