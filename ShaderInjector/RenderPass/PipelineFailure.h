#pragma once

#include <cstdint>
#include <string>

namespace RenderPassReplacement
{
	//remember a pipeline failure only for the shader generation that produced it.
	struct PipelineFailure
	{
		uint64_t generation = 0;
		std::string error;
	};
} //namespace RenderPassReplacement
