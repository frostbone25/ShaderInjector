#pragma once

#include <cstddef>
#include <cstdint>

#include "ModifiedShader/ModifiedShaderPackageDisk.h"

namespace ModifiedShaderRecapture
{
	//refresh the fingerprint for this original shader, keeping package settings and other game-version fingerprints.
	bool UpdateFingerprint(ModifiedShader::ModifiedShaderPackageDisk& package, uint64_t shaderHash, size_t bytecodeLength, const ShaderAnalysis::ShaderAnalysisDisk& analysis);

	//graphics shaders can be shared by different PSOs; cache aliases are only reusable when the saved states agree.
	bool MatchesGraphicsPipelineState(const ShaderTarget::ShaderTargetDisk& saved, const ShaderTarget::ShaderTargetDisk& captured);
}
