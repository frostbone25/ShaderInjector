#pragma once

namespace Hooks 
{
	// Installs an early DXGI factory hook so every D3D12 swap chain can be tied
	// to the exact command queue supplied at creation. When OptiScaler is present,
	// the same hook also installs the object-local Present compatibility wrapper.
	bool PrepareSwapChainCapture();
	bool IsOptiScalerCompatibilityEnabled();

	extern void Initialize();

	void CleanupDummyObjects();
}
