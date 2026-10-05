#pragma once

#include "HookD3D12.h"
#include "D3D12/HookD3D12RuntimeState.h"

namespace HookD3D12
{
	struct OverlayInitializationScope
	{
		//release partial heaps and allocators before another initialization attempt can overwrite them.
		~OverlayInitializationScope()
		{
			if (!gInitialized)
				ReleaseOverlaySwapChainResources(true);
		}
	};
}
