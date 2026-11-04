#pragma once

#include "ShaderDiscovery/TargetCreationJob.h"
#include "ShaderTarget/ShaderTarget.h"

namespace ShaderAutomaticDiscovery
{
	//return prepared disk data for render-thread publication.
	struct TargetCreationResult
	{
		TargetCreationJob job;
		ShaderTarget::ShaderTargetDisk preparedTarget{};
		bool succeeded = false;
	};
} //namespace ShaderAutomaticDiscovery
