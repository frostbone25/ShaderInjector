#pragma once

#include <cstddef>

namespace HookD3D12
{
	struct HookDefinition
	{
		size_t vTableIndex;
		void* hookFunction;
		void** originalFunction;
	};
}
