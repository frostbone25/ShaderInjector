//HookInput.cpp
#include "HookInput.h"

//custom
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
	static std::map<HWND, WNDPROC> sOriginalWindowProcedures;

	void Initalize(HWND windowHandle)
	{
		//DebugLog("[inputhook] Initializing input hook for window %p\n", windowHandle);

		// Skip windows that were already subclassed by this DLL.
		if (sOriginalWindowProcedures.count(windowHandle)) 
		{
			//DebugLog("[inputhook] Window %p is already hooked, skipping.\n", windowHandle);
			return;
		}

		// The GUI and shutdown paths need to know which game window currently owns ImGui input.
		Globals::mainWindow = windowHandle;

		WNDPROC originalWindowProcedure = (WNDPROC)SetWindowLongPtr(windowHandle, GWLP_WNDPROC, (LONG_PTR)WndProc);

		if (!originalWindowProcedure) 
		{
			//DebugLog("[inputhook] Failed to set WndProc for window %p: %d\n", windowHandle, GetLastError());
		}
		else 
		{
			sOriginalWindowProcedures[windowHandle] = originalWindowProcedure;
			//DebugLog("[inputhook] WndProc hook set for window %p. Original WndProc=%p\n", windowHandle, originalWindowProcedure);
		}
	}

	void Remove(HWND windowHandle)
	{
		auto originalWindowProcedureIterator = sOriginalWindowProcedures.find(windowHandle);

		if (originalWindowProcedureIterator == sOriginalWindowProcedures.end()) 
		{
			//DebugLog("[inputhook] WndProc hook for window %p was already removed or never set.\n", windowHandle);
			return;
		}

		//DebugLog("[inputhook] Removing input hook for window %p\n", windowHandle);

		if (SetWindowLongPtr(windowHandle, GWLP_WNDPROC, (LONG_PTR)originalWindowProcedureIterator->second) == 0) 
		{
			//DebugLog("[inputhook] Failed to restore WndProc for window %p: %d\n", windowHandle, GetLastError());
		}
		else 
		{
			//DebugLog("[inputhook] WndProc restored to %p for window %p\n", originalWindowProcedureIterator->second, windowHandle);
		}

		sOriginalWindowProcedures.erase(originalWindowProcedureIterator);

		// Only clear the shared window handle when this hook owned it.
		if (Globals::mainWindow == windowHandle)
			Globals::mainWindow = nullptr;
	}

	LRESULT APIENTRY WndProc(HWND windowHandle, UINT message, WPARAM wordParameter, LPARAM longParameter)
	{
		// Messages must be forwarded to the original WndProc for the same HWND that received them.
		auto originalWindowProcedureIterator = sOriginalWindowProcedures.find(windowHandle);
		WNDPROC originalWindowProcedure = (originalWindowProcedureIterator != sOriginalWindowProcedures.end()) ? originalWindowProcedureIterator->second : nullptr;

		const bool isFreshKeyDown = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && (longParameter & 0x40000000) == 0;

		if (isFreshKeyDown)
		{
			const int key = static_cast<int>(wordParameter);

			if (key == Globals::keyOpenShaderInjectorGUI)
			{
				Globals::gShowShaderInjectorGUI = !Globals::gShowShaderInjectorGUI;
				return TRUE;
			}

			if (key == Globals::keyToggleShaderInjector)
			{
				Globals::gShaderInjectorEnabled = !Globals::gShaderInjectorEnabled;
				HookD3D12::MarkShaderTargetApplyDirty();
				return TRUE;
			}
		}

		if (Globals::gShowShaderInjectorGUI && HookD3D12::IsInitialized() && ImGui::GetCurrentContext())
		{
			if (ImGui_ImplWin32_WndProcHandler(windowHandle, message, wordParameter, longParameter))
				return TRUE;

			ImGuiIO& imguiInputOutput = ImGui::GetIO();

			// When ImGui wants input, swallow it so the game does not also react to menu clicks/typing.
			if (imguiInputOutput.WantCaptureMouse || imguiInputOutput.WantCaptureKeyboard)
				return TRUE;
		}

		if (originalWindowProcedure)
			return CallWindowProc(originalWindowProcedure, windowHandle, message, wordParameter, longParameter);

		return DefWindowProc(windowHandle, message, wordParameter, longParameter);
	}
}
