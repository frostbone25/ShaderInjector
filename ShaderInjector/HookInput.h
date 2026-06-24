#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <map>
#include <unordered_map>
#include <Psapi.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <d3d9.h>
#include <d3d10_1.h>
#include <d3d10.h>
#include <d3d11.h>
#include <d3d12.h>

//minhook
#include "MinHook.h"

//imgui
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

//custom
#include "PreCompiledHeader.h" //stupid precompiled header
#include "Hooks.h"
#include "Globals.h"
#include "dsound_proxy.h"
#include "HookD3D12.h"

namespace HookInput
{
	extern void Initalize(HWND hWindow);
	extern void Remove(HWND hWindow);

	static LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
}
