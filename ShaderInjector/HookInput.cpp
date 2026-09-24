#include "HookInput.h"

#include <map>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "Globals.h"
#include "HookD3D12.h"

//the imgui Win32 backend keeps this declaration disabled in its header to avoid a Windows include.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND windowHandle, UINT message, WPARAM wordParameter, LPARAM longParameter);

namespace HookInput
{
	//each window keeps its own original procedure so messages and removal go to the right owner.
	static std::map<HWND, WNDPROC> sOriginalWindowProcedures;

	void Initialize(HWND windowHandle)
	{
		//a window can appear more than once during swap-chain discovery, so hook it only once.
		if (sOriginalWindowProcedures.count(windowHandle))
			return;

		//the overlay needs the active game window for input and shutdown.
		Globals::mainWindow = windowHandle;

		WNDPROC originalWindowProcedure = (WNDPROC)SetWindowLongPtr(windowHandle, GWLP_WNDPROC, (LONG_PTR)WndProc);

		if (originalWindowProcedure)
			sOriginalWindowProcedures[windowHandle] = originalWindowProcedure;
	}

	void Remove(HWND windowHandle)
	{
		auto originalWindowProcedureIterator = sOriginalWindowProcedures.find(windowHandle);

		if (originalWindowProcedureIterator == sOriginalWindowProcedures.end())
			return;

		//restore the procedure before forgetting its mapping.
		SetWindowLongPtr(windowHandle, GWLP_WNDPROC, (LONG_PTR)originalWindowProcedureIterator->second);
		sOriginalWindowProcedures.erase(originalWindowProcedureIterator);

		//another window may have become active since this one was hooked.
		if (Globals::mainWindow == windowHandle)
			Globals::mainWindow = nullptr;
	}

	LRESULT APIENTRY WndProc(HWND windowHandle, UINT message, WPARAM wordParameter, LPARAM longParameter)
	{
		//forward messages to the procedure captured for this specific window.
		auto originalWindowProcedureIterator = sOriginalWindowProcedures.find(windowHandle);
		WNDPROC originalWindowProcedure = nullptr;
		if (originalWindowProcedureIterator != sOriginalWindowProcedures.end())
			originalWindowProcedure = originalWindowProcedureIterator->second;

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

			//when the overlay owns input, keep its clicks and typing away from the game.
			if (imguiInputOutput.WantCaptureMouse || imguiInputOutput.WantCaptureKeyboard)
				return TRUE;
		}

		if (originalWindowProcedure)
			return CallWindowProc(originalWindowProcedure, windowHandle, message, wordParameter, longParameter);

		return DefWindowProc(windowHandle, message, wordParameter, longParameter);
	}
} //namespace HookInput
