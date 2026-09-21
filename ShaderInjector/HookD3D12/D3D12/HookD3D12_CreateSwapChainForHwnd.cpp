#include "HookD3D12SwapChainHookHandlers.h"

namespace Hooks
{
	HRESULT STDMETHODCALLTYPE Hook_CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* description, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDescription, IDXGIOutput* restrictToOutput, IDXGISwapChain1** swapChain)
	{
		return Handle_CreateSwapChainForHwnd(factory, device, window, description, fullscreenDescription, restrictToOutput, swapChain);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* description, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDescription, IDXGIOutput* restrictToOutput, IDXGISwapChain1** swapChain)
	{
		HRESULT result = gOriginalCreateSwapChainForHwnd
			? gOriginalCreateSwapChainForHwnd(factory, device, window, description, fullscreenDescription, restrictToOutput, swapChain)
			: E_POINTER;

		const bool hasExplicitSize = description && description->Width != 0 && description->Height != 0;
		const bool isOverlaySizedSwapChain = hasExplicitSize && (description->Width < 100 || description->Height < 100);

		if (SUCCEEDED(result) && swapChain && *swapChain && !isOverlaySizedSwapChain)
			CaptureCreatedSwapChain(device, *swapChain);

		return result;
	}
}
