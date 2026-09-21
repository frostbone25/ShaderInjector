#include "../HookD3D12.h"

namespace HookD3D12
{
	HRESULT STDMETHODCALLTYPE Hook_CreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* description, IDXGISwapChain** swapChain)
	{
		return Handle_CreateSwapChain(factory, device, description, swapChain);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* description, IDXGISwapChain** swapChain)
	{
		HRESULT result = gOriginalCreateSwapChain ? gOriginalCreateSwapChain(factory, device, description, swapChain) : E_POINTER;
		const bool hasExplicitSize = description && description->BufferDesc.Width != 0 && description->BufferDesc.Height != 0;
		const bool isOverlaySizedSwapChain = hasExplicitSize && (description->BufferDesc.Width < 100 || description->BufferDesc.Height < 100);

		if (SUCCEEDED(result) && swapChain && *swapChain && !isOverlaySizedSwapChain)
			CaptureCreatedSwapChain(device, *swapChain);

		return result;
	}

}
