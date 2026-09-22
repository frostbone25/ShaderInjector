#pragma once

#include <windows.h>
#include <dxgi1_4.h>

namespace HookD3D12
{
	struct OverlayStartupGateState
	{
		HWND outputWindow = nullptr;
		UINT bufferCount = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		UINT flags = 0;
		LONG clientWidth = 0;
		LONG clientHeight = 0;
		int stableFrames = 0;
		ULONGLONG firstStableTick = 0;
	};
}
