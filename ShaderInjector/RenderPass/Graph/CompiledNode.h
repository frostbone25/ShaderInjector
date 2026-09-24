#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Enum/RenderPassGraphBoundary.h"
#include "RenderPass/RenderPass.h"

namespace RenderPassGraph
{
	inline constexpr size_t invalidNodeIndex = static_cast<size_t>(-1);

	//store a compiled pass and its dependencies after graph validation.
	struct CompiledNode
	{
		bool valid = false;
		size_t renderPassIndex = invalidNodeIndex;
		size_t parentRenderPassIndex = invalidNodeIndex;
		std::string modifiedShaderId;
		Boundary rootBoundary = Boundary::Before;
		RenderPass::ExecutionMode executionMode = RenderPass::ExecutionMode::Automatic;
		RenderPass::PassOperation operation = RenderPass::PassOperation::Automatic;
		std::vector<size_t> dependencies;
		std::string error;
	};
} //namespace RenderPassGraph
