#pragma once
#include "Enum/RenderPassGraphBoundary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "RenderPass/RenderPass.h"
#include "RenderPass/Graph/CompiledNode.h"
#include "RenderPass/Graph/Compilation.h"
#include "RenderPass/Graph/ExecutionPlan.h"

namespace RenderPassGraph
{
	Compilation Compile(const std::vector<RenderPass::RenderPassDisk>& renderPasses);
} //namespace RenderPassGraph
