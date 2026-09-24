#pragma once

#include <cstddef>

namespace HookD3D12
{
	//point a captured shader identity back to the pipeline that supplied its bytecode.
	struct CapturedShaderLocation
	{
		bool isStreamPipeline = false;
		size_t pipelineIndex = 0;
	};
} //namespace HookD3D12
