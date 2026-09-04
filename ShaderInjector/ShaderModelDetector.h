#pragma once

#include <cstddef>

#include "Globals.h"
#include "ShaderTarget/ShaderTarget.h"

namespace ShaderModelDetector
{
	// Records the shader model encoded in an original DXBC or DXIL container.
	// This is intentionally allocation-free because it runs from PSO creation hooks.
	void ObserveShaderBytecode(
		ShaderTarget::ShaderType expectedShaderType,
		const void* shaderBytecode,
		size_t shaderBytecodeSize);

	// Returns the most frequently observed model for a stage. The injector can
	// create a few of its own shaders, so using the dominant model avoids letting
	// one internal PSO determine the renderer-wide compiler setting.
	bool TryGetDetectedShaderModel(
		ShaderTarget::ShaderType shaderType,
		Globals::ShaderModel& detectedShaderModel);

	// Automatic detection leaves the configured value intact as a portable
	// fallback. Disabling detection immediately restores that configured value.
	Globals::ShaderModel GetEffectiveShaderModel(
		ShaderTarget::ShaderType shaderType,
		Globals::ShaderModel configuredShaderModel);
}
