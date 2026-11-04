#pragma once

#include <string>
#include <memory>
#include <vector>

#include "ShaderAnalysis/ShaderAnalysis.h"
#include "ShaderDiscovery/QueuedShader.h"
#include "ModifiedShader/ModifiedShaderPackageDisk.h"

namespace ShaderAutomaticDiscovery
{
	//send the worker's match and reflection data back to the main discovery flow.
	struct AnalysisResult
	{
		QueuedShader queuedShader;
		std::string modifiedShaderID;
		ShaderAnalysis::ShaderAnalysisDisk shaderAnalysis;
		std::shared_ptr<const std::vector<ModifiedShader::ModifiedShaderPackageDisk>> modifiedShaders;
	};
} //namespace ShaderAutomaticDiscovery
