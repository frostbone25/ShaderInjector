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
	std::string runtimeLogText;

	static bool injectorDeveloperSettings = false;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void DrawMainWindow(const MainWindowContext& context)
	{
		if (!context.showWindow || !*context.showWindow)
			return;

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(25, 25), ImGuiCond_FirstUseEver);

		std::string windowTitle = std::string("Shader Injector v") + SHADER_INJECTOR_VERSION_STRING;

		//shader injector window start
		if (ImGui::Begin(windowTitle.c_str(), context.showWindow, flags))
		{
			//injector enable checkbox
			ImGui::Checkbox("##InjectorEnabled", &Globals::gShaderInjectorEnabled);
			ImGui::SameLine();
			ImGui::Text("Injector %s", context.injectorEnabled ? "Enabled!" : "Disabled!");

			//fps counter
			if (context.fpsCounterActive)
				ImGui::Text("FPS: %.1f (%.4fms)", context.fps, context.frameTimeMs);

			const std::string toggleInjectorKeyText = Keycodes::KeycodeToString(Globals::keyToggleShaderInjector) + " (" + std::to_string(Globals::keyToggleShaderInjector) + ")";
			const std::string toggleMenuKeyText = Keycodes::KeycodeToString(Globals::keyOpenShaderInjectorGUI) + " (" + std::to_string(Globals::keyOpenShaderInjectorGUI) + ")";

			ImGui::Text("Toggle Injector: Press %s", toggleInjectorKeyText.c_str());
			ImGui::Text("Toggle Menu: Press %s", toggleMenuKeyText.c_str());

			ImGui::SetNextItemWidth(140.0f * Globals::gShaderInjectorGUIScale);

			if (ImGui::DragFloat(
				"Menu Scale",
				&Globals::gShaderInjectorGUIScale,
				0.05f,
				0.5f,
				4.0f,
				"%.2fx",
				ImGuiSliderFlags_AlwaysClamp))
			{
				Globals::gShaderInjectorGUIScale = (std::clamp)(Globals::gShaderInjectorGUIScale, 0.5f, 4.0f);
			}

			if (ImGui::IsItemDeactivatedAfterEdit() && !ShaderInjectorIO::WriteInjectorMenuScale(Globals::gShaderInjectorGUIScale))
			{
				WriteToRuntimeLogError("Could not save MenuScale to ShaderInjector.ini.");
			}

			if (ImGui::Button("Edit Injector Settings", ImVec2(-FLT_MIN, 0)) && !ShaderInjectorIO::OpenFile( ShaderInjectorIO::GetInjectorSettingsPath()))
			{
				WriteToRuntimeLogError("Could not open ShaderInjector.ini.");
			}

			ImGui::Spacing();

			//set in HookD3D12, wires up an event that calls UI_ShaderInjectorMenu()
			if (context.drawMenu)
				context.drawMenu();

			//log section
			ImGui::BeginGroup();
			ImGui::SeparatorText("Log");
			if (ImGui::TreeNodeEx("Runtime Log"))
			{
				if (ImGui::Button("Clear Log"))
					ClearRuntimeLog();

				if (context.runtimeLogText)
				{
					const std::string runtimeLogSnapshot = GetRuntimeLogSnapshot();
					ImGui::TextUnformatted(runtimeLogSnapshot.c_str());
				}

				ImGui::TreePop();
			}
			ImGui::EndGroup();
		} //window end

		ImGui::End();
	}

	void UI_ShaderInjectorMenu()
	{
		UI_ModifiedShaders();
		UI_ShaderConfiguration();
		UI_ShaderTargets();
		UI_RenderPasses();
		UI_DeveloperSettings();
	}
}
