#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "RenderPass/Graph/CompiledNode.h"
#include "RenderPass/Graph/ExecutionPlan.h"

namespace RenderPassGraph
{
	//publish validated nodes, per-shader plans, and diagnostics as one result.
	struct Compilation
	{
		std::unordered_map<std::string, size_t> renderPassIndices;
		std::vector<CompiledNode> nodes;
		std::unordered_map<std::string, ExecutionPlan> executionPlans;
		std::vector<std::string> diagnostics;
	};
} //namespace RenderPassGraph
