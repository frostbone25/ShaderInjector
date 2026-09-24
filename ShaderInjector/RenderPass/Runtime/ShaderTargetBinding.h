#pragma once

#include <cstdint>
#include <string>

#include "Enum/ShaderType.h"
#include "RenderPass/PipelineOutputState.h"

namespace RenderPassRuntime
{
	//link a live pipeline state to the modified shader and output layout it represents.
	struct ShaderTargetBinding
	{
		std::string modifiedShaderId;
		std::string name;
		uint64_t hash = 0;
		ShaderTarget::ShaderType type = ShaderTarget::Unknown;
		PipelineOutputState outputState;
	};
} //namespace RenderPassRuntime
