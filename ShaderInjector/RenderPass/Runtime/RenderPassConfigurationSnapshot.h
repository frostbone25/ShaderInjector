#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "RenderPass/RenderPassGraph.h"
#include "RenderPass/Runtime/ModifiedShaderExecutionPlan.h"
#include "RenderPass/Runtime/ResolvedEventBinding.h"
#include "RenderPass/Runtime/RuntimeCounters.h"

namespace RenderPassRuntime
{
	//publish a complete pass graph so hook threads never see a half-updated configuration.
	struct RenderPassConfigurationSnapshot
	{
		std::vector<RenderPass::RenderPassDisk> renderPasses;
		std::vector<std::unique_ptr<RuntimeCounters>> runtimeCounters;
		RenderPassGraph::Compilation compiledGraph;
		std::unordered_map<std::string, size_t> renderPassIndices;
		std::vector<ResolvedEventBinding> resolvedEvents;
		std::unordered_map<std::string, ModifiedShaderExecutionPlan> executionPlans;
	};
} //namespace RenderPassRuntime
