#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "RenderPass/Runtime/RequiredGameInput.h"

namespace RenderPassRuntime
{
	//group a modified shader's pass order and required inputs by execution boundary.
	struct ModifiedShaderExecutionPlan
	{
		std::array<std::vector<const RenderPass::RenderPassDisk*>, 2> executionOrders;
		std::array<std::vector<const RenderPass::RenderPassDisk*>, 2> mipChainOrders;
		std::array<std::vector<RequiredGameInput>, 2> requiredGameInputs;
		uint32_t graphicsBoundaryMask = 0;
		uint32_t computeBoundaryMask = 0;
		bool hasRuntimeResources = false;
	};
} //namespace RenderPassRuntime
