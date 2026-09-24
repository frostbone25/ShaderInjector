#pragma once

#include <memory>
#include <vector>

#include "ModifiedShader/ModifiedShaderPackageDisk.h"
#include "ShaderAutomaticDiscovery/QueuedShader.h"

namespace ShaderAutomaticDiscovery
{
	//a worker receives the shader and the package snapshot used for this analysis batch.
	struct AnalysisJob
	{
		QueuedShader queuedShader;
		std::shared_ptr<const std::vector<ModifiedShader::ModifiedShaderPackageDisk>> modifiedShaders;
	};
} //namespace ShaderAutomaticDiscovery
