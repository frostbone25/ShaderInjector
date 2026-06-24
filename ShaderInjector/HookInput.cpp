#include "HookInput.h"
#include "Globals.h"
#include "HookD3D12.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace HookInput
{
	// Maps each hooked window handle to its original WndProc.
	// Using a map instead of a single static variable ensures that:
	//   - Each window correctly forwards messages to its own original WndProc.
	//   - Remove() restores every hooked window individually.
	//   - No window is left pointing to unloaded DLL code after removal.
	static std::map<HWND, WNDPROC> sOriginalWndProcs;

	void Initalize(HWND hWindow)
	{
		//DebugLog("[inputhook] Initializing input hook for window %p\n", hWindow);

		// Skip if this window is already hooked
		if (sOriginalWndProcs.count(hWindow)) 
		{
			//DebugLog("[inputhook] Window %p is already hooked, skipping.\n", hWindow);
			return;
		}

		// Store window globally for later use during release
		Globals::mainWindow = hWindow;

		WNDPROC original = (WNDPROC)SetWindowLongPtr(hWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);

		if (!original) 
		{
			//DebugLog("[inputhook] Failed to set WndProc for window %p: %d\n", hWindow, GetLastError());
		}
		else 
		{
			sOriginalWndProcs[hWindow] = original;
			//DebugLog("[inputhook] WndProc hook set for window %p. Original WndProc=%p\n", hWindow, original);
		}
	}

	void Remove(HWND hWindow)
	{
		auto it = sOriginalWndProcs.find(hWindow);

		if (it == sOriginalWndProcs.end()) 
		{
			//DebugLog("[inputhook] WndProc hook for window %p was already removed or never set.\n", hWindow);
			return;
		}

		//DebugLog("[inputhook] Removing input hook for window %p\n", hWindow);

		if (SetWindowLongPtr(hWindow, GWLP_WNDPROC, (LONG_PTR)it->second) == 0) 
		{
			//DebugLog("[inputhook] Failed to restore WndProc for window %p: %d\n", hWindow, GetLastError());
		}
		else 
		{
			//DebugLog("[inputhook] WndProc restored to %p for window %p\n", it->second, hWindow);
		}

		sOriginalWndProcs.erase(it);

		// Clear the global handle only if it matched this window
		if (Globals::mainWindow == hWindow)
			Globals::mainWindow = nullptr;
	}

	LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		// Look up the original WndProc for this specific window
		auto it = sOriginalWndProcs.find(hwnd);
		WNDPROC original = (it != sOriginalWndProcs.end()) ? it->second : nullptr;

		if (Globals::gShowShaderInjectorGUI && HookD3D12::IsInitialized() && ImGui::GetCurrentContext())
		{
			if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
				return TRUE;

			ImGuiIO& io = ImGui::GetIO();

			if (io.WantCaptureMouse || io.WantCaptureKeyboard)
			{
				return TRUE;

				/*
				switch (uMsg)
				{
				case WM_KEYUP:
				case WM_SYSKEYUP:
				case WM_LBUTTONUP:
				case WM_RBUTTONUP:
				case WM_MBUTTONUP:
				case WM_XBUTTONUP:
					if (original)
						return CallWindowProc(original, hwnd, uMsg, wParam, lParam);
					return DefWindowProc(hwnd, uMsg, wParam, lParam);
				default:
					return TRUE;
				}
				*/
			}
		}

		if (original)
			return CallWindowProc(original, hwnd, uMsg, wParam, lParam);
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}
