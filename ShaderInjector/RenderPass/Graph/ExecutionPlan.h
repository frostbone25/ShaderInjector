#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RenderPassGraph
{
	//group pass order and mip-chain order by before and after boundaries.
	struct ExecutionPlan
	{
		std::array<std::vector<size_t>, 2> executionOrders;
		std::array<std::vector<size_t>, 2> mipChainOrders;
		uint32_t graphicsBoundaryMask = 0;
		uint32_t computeBoundaryMask = 0;
	};
} //namespace RenderPassGraph
