//ShaderInjectorGUI.cpp
#include "ShaderInjectorGUI.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <mutex>
#include <vector>

//3RD Party
#include "imgui.h"

//custom
#include "HookD3D12.h"
#include "ShaderInjectorIO.h"
#include "Hash.h"
#include "Globals.h"
#include "DatabaseModifiedShaders.h"
#include "DatabaseRenderPasses.h"
#include "DatabaseShaderConfigurations.h"
#include "ModifiedShaderCreation.h"
#include "RenderDocIntegration.h"
#include "RenderPassRuntime.h"
#include "ShaderAutomaticDiscovery.h"
#include "StringHelper.h"
#include "ShaderInjectorGUITooltips.h"
#include "Keycodes.h"
#include "ShaderInjectorVersion.h"

namespace ShaderInjectorGUI
{
	void UI_ApplyStyle()
	{
		ImGuiStyle& imguiStyle = ImGui::GetStyle();
		imguiStyle = ImGuiStyle();
		ImGui::StyleColorsDark(&imguiStyle);

		//cool rounding and spacing!
		imguiStyle.WindowRounding = 6.0f;
		imguiStyle.FrameRounding = 4.0f;
		imguiStyle.ScrollbarRounding = 4.0f;
		imguiStyle.GrabRounding = 4.0f;
		imguiStyle.TabRounding = 4.0f;
		imguiStyle.WindowPadding = ImVec2(10.0f, 10.0f);
		imguiStyle.FramePadding = ImVec2(6.0f, 4.0f);
		imguiStyle.ItemSpacing = ImVec2(8.0f, 6.0f);
		imguiStyle.ScrollbarSize = 12.0f;

		ImVec4* imguiStyleColors = imguiStyle.Colors;

		//accents for hover / active / grab
		const ImVec4 accent = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
		const ImVec4 accentHovered = ImVec4(0.35f, 0.55f, 0.80f, 1.00f);
		const ImVec4 accentActive = ImVec4(0.20f, 0.38f, 0.62f, 1.00f);

		//text
		imguiStyleColors[ImGuiCol_Text] = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
		imguiStyleColors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

		//window, background surfaces
		imguiStyleColors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
		imguiStyleColors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
		imguiStyleColors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

		//borders
		imguiStyleColors[ImGuiCol_Border] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		imguiStyleColors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

		//title bar
		imguiStyleColors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		imguiStyleColors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		imguiStyleColors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.08f, 0.75f);

		//frames (inputText, sliders)
		imguiStyleColors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
		imguiStyleColors[ImGuiCol_FrameBgHovered] = accentHovered;
		imguiStyleColors[ImGuiCol_FrameBgActive] = accentActive;

		//buttons
		imguiStyleColors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		imguiStyleColors[ImGuiCol_ButtonHovered] = accentHovered;
		imguiStyleColors[ImGuiCol_ButtonActive] = accentActive;

		//headers (collapsingHeader, selectable, treeNode)
		imguiStyleColors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		imguiStyleColors[ImGuiCol_HeaderHovered] = accentHovered;
		imguiStyleColors[ImGuiCol_HeaderActive] = accentActive;

		//tabs ---
		imguiStyleColors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
		imguiStyleColors[ImGuiCol_TabHovered] = accentHovered;
		imguiStyleColors[ImGuiCol_TabActive] = accent;
		imguiStyleColors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
		imguiStyleColors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

		//checkmark / slider grab
		imguiStyleColors[ImGuiCol_CheckMark] = accentHovered;
		imguiStyleColors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
		imguiStyleColors[ImGuiCol_SliderGrabActive] = accentActive;

		//scrollbar
		imguiStyleColors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		imguiStyleColors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
		imguiStyleColors[ImGuiCol_ScrollbarGrabHovered] = accentHovered;
		imguiStyleColors[ImGuiCol_ScrollbarGrabActive] = accentActive;

		//resize grip
		imguiStyleColors[ImGuiCol_ResizeGrip] = ImVec4(0.22f, 0.22f, 0.22f, 0.50f);
		imguiStyleColors[ImGuiCol_ResizeGripHovered] = accentHovered;
		imguiStyleColors[ImGuiCol_ResizeGripActive] = accentActive;

		//separator
		imguiStyleColors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
		imguiStyleColors[ImGuiCol_SeparatorHovered] = accentHovered;
		imguiStyleColors[ImGuiCol_SeparatorActive] = accentActive;

		//other
		imguiStyleColors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
		imguiStyleColors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
		imguiStyleColors[ImGuiCol_NavHighlight] = accent;

		//Rebuild the style from an unscaled baseline every frame so live scale changes never
		//compound the previous frame's dimensions.
		const float menuScale = (std::clamp)(Globals::gShaderInjectorGUIScale, 0.5f, 4.0f);
		imguiStyle.ScaleAllSizes(menuScale);
		imguiStyle.FontScaleMain = menuScale;
	}
}