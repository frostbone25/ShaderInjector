#pragma once

#include <cstdint>
#include <vector>

#include "Enum/RenderPassRuntimeRootBindingType.h"

namespace RenderPassRuntime
{
	//keep the latest value for one root argument on a command list.
	struct RootBindingState
	{
		RootBindingType type = RootBindingType::None;
		uint64_t value = 0;
		std::vector<uint32_t> constants;
	};
} //namespace RenderPassRuntime
