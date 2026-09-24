#pragma once

#include <cstddef>

namespace HookD3D12
{
	struct CachedBlobContentMatch
	{
		double matchingRatio = 0.0;
		size_t longestMatchingRun = 0;
	};
} //namespace HookD3D12
