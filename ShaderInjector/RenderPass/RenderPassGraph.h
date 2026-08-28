#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "RenderPass/RenderPass.h"

namespace RenderPassGraph
{
	inline constexpr size_t invalidNodeIndex = static_cast<size_t>(-1);

	enum class Boundary : uint8_t
	{
		Before,
		After,
	};

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

	struct ExecutionPlan
	{
		std::array<std::vector<size_t>, 2> executionOrders;
		std::array<std::vector<size_t>, 2> mipChainOrders;
		uint32_t graphicsBoundaryMask = 0;
		uint32_t computeBoundaryMask = 0;
	};

	struct Compilation
	{
		std::unordered_map<std::string, size_t> renderPassIndices;
		std::vector<CompiledNode> nodes;
		std::unordered_map<std::string, ExecutionPlan> executionPlans;
		std::vector<std::string> diagnostics;
	};

	Compilation Compile(const std::vector<RenderPass::RenderPassDisk>& renderPasses);
}
