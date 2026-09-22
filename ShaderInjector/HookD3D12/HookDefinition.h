#pragma once

#include <cstddef>

namespace HookD3D12
{
	struct HookDefinition
	{
		size_t vtableIndex;
		void* hookFunction;
		void** originalFunction;
	};
}
