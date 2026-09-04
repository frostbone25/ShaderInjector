#pragma once

#include <dxgi1_4.h>

namespace Hooks
{
	using FunctionCreateSwapChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	using FunctionCreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

	extern FunctionCreateSwapChain gOriginalCreateSwapChain;
	extern FunctionCreateSwapChainForHwnd gOriginalCreateSwapChainForHwnd;
	void CaptureCreatedSwapChain(IUnknown* creationDevice, IUnknown* swapChain);

	HRESULT STDMETHODCALLTYPE Hook_CreateSwapChain(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	HRESULT STDMETHODCALLTYPE Hook_CreateSwapChainForHwnd(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

	HRESULT STDMETHODCALLTYPE Handle_CreateSwapChain(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	HRESULT STDMETHODCALLTYPE Handle_CreateSwapChainForHwnd(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
}
