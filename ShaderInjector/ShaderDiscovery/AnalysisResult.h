#pragma once

#include <string>

#include "ShaderAnalysis/ShaderAnalysis.h"
#include "ShaderDiscovery/QueuedShader.h"

namespace ShaderAutomaticDiscovery
{
	//send the worker's match and reflection data back to the main discovery flow.
	struct AnalysisResult
	{
		QueuedShader queuedShader;
		std::string modifiedShaderID;
		ShaderAnalysis::ShaderAnalysisDisk shaderAnalysis;
	};
} //namespace ShaderAutomaticDiscovery
