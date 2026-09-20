#pragma once

#include <cstdint>

namespace RenderPassGraph
{
	enum class VisitState : uint8_t
	{
		Unvisited,
		Visiting,
		Complete,
	};
}
