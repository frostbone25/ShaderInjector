#pragma once

namespace HookD3D12
{
	extern thread_local bool gInsideSwapChainCompatibilityCall;

	class ScopedSwapChainCompatibilityCall
	{
	public:
		ScopedSwapChainCompatibilityCall()
		{
			gInsideSwapChainCompatibilityCall = true;
		}

		~ScopedSwapChainCompatibilityCall()
		{
			gInsideSwapChainCompatibilityCall = false;
		}
	};
}
