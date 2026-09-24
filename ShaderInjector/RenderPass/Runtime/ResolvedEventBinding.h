#pragma once

#include <string>

#include "Enum/RenderPassRuntimeExecutionBoundary.h"

namespace RenderPassRuntime
{
	//cache the event boundary found while compiling one pass configuration.
	struct ResolvedEventBinding
	{
		bool valid = false;
		std::string modifiedShaderId;
		ExecutionBoundary rootBoundary = ExecutionBoundary::Before;
	};
} //namespace RenderPassRuntime
