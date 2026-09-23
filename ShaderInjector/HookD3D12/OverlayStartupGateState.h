#pragma once

#include <windows.h>
#include <dxgi1_4.h>

namespace HookD3D12
{
	struct OverlayStartupGateState
	{
		HWND outputWindowHandle = nullptr;
		UINT bufferCount = 0;
		DXGI_FORMAT swapChainFormat = DXGI_FORMAT_UNKNOWN;
		UINT swapChainFlags = 0;
		LONG clientWidth = 0;
		LONG clientHeight = 0;
		int stableFrameCount = 0;
		ULONGLONG firstStableTick = 0;
	};
}
