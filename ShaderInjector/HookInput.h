#pragma once
#include <windows.h>

namespace HookInput
{
	void Initialize(HWND windowHandle);
	void Remove(HWND windowHandle);

	LRESULT APIENTRY WndProc(HWND windowHandle, UINT message, WPARAM wordParameter, LPARAM longParameter);
} //namespace HookInput
