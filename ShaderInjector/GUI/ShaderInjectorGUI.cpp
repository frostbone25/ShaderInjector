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
#include "IO/ShaderInjectorIO.h"
#include "Hash/Hash.h"
#include "Globals.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ModifiedShader/ModifiedShaderCreation.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "RenderPass/RenderPassRuntime.h"
#include "ShaderDiscovery/ShaderAutomaticDiscovery.h"
#include "StringHelper.h"
#include "GUI/ShaderInjectorGUITooltips.h"
#include "Input/Keycodes.h"
#include "ShaderInjectorVersion.h"

namespace
{
	bool DrawKeycodeCombo(const char* label, int& keycode)
	{
		const std::string preview = Keycodes::KeycodeToString(keycode) + " (" + std::to_string(keycode) + ")";
		bool changed = false;

		//scope the hidden combo ID by its label so the two hotkey controls stay independent.
		ImGui::PushID(label);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f * Globals::gShaderInjectorGUIScale);

		if (ImGui::BeginCombo("##Keycode", preview.c_str()))
		{
			for (const Keycodes::KeycodeOption& option : Keycodes::SelectableKeycodes())
			{
				const bool selected = keycode == option.value;
				const std::string optionLabel = option.name + " (" + std::to_string(option.value) + ")";

				if (ImGui::Selectable(optionLabel.c_str(), selected))
				{
					keycode = option.value;
					changed = true;
				}

				if (selected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		ImGui::PopID();

		return changed;
	}
} //namespace

namespace ShaderInjectorGUI
{
	std::string runtimeLogText;

	static bool injectorDeveloperSettings = false;

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
			bool injectorEnabled = Globals::gShaderInjectorEnabled.load(std::memory_order_acquire);

			if (ImGui::Checkbox("##InjectorEnabled", &injectorEnabled))
				Globals::gShaderInjectorEnabled.store(injectorEnabled, std::memory_order_release);
			ImGui::SameLine();
			const char* injectorStatus = "Disabled!";

			if (context.injectorEnabled)
				injectorStatus = "Enabled!";

			ImGui::Text("Injector %s", injectorStatus);

			//fps counter
			if (context.framesPerSecondCounterActive)
				ImGui::Text("FPS: %.1f (%.4fms)", context.framesPerSecond, context.frameTimeMilliseconds);

			//keep the mode selector available in both views, directly below injector status and fps.
			int menuModeIndex = 0;

			if (Globals::gShaderInjectorAdvancedMode)
				menuModeIndex = 1;

			//draw the label first and hide the combo's own label so the control sits on its right.
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Mode");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f * Globals::gShaderInjectorGUIScale);

			if (ImGui::Combo("##MenuMode", &menuModeIndex, "Basic\0Advanced\0"))
			{
				Globals::gShaderInjectorAdvancedMode = menuModeIndex == 1;

				//persist mode changes immediately so the next launch uses the selected view.
				if (!ShaderInjectorIO::WriteInjectorMenuMode(Globals::gShaderInjectorAdvancedMode))
					WriteToRuntimeLogError("Could not save MenuMode to ShaderInjector.ini.");
			}

			//keep injector settings first and available in both menu modes.
			if (ImGui::CollapsingHeader("Injector Settings"))
			{
				ImGui::Indent(indentSpace);
				ImGui::Spacing();

				DrawKeycodeCombo("Toggle Injector", Globals::keyToggleShaderInjector);
				DrawKeycodeCombo("Toggle Menu", Globals::keyOpenShaderInjectorGUI);

				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Menu Scale");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(140.0f * Globals::gShaderInjectorGUIScale);

				if (ImGui::DragFloat("##MenuScale", &Globals::gShaderInjectorGUIScale, 0.05f, 0.5f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
					Globals::gShaderInjectorGUIScale = (std::clamp)(Globals::gShaderInjectorGUIScale, 0.5f, 4.0f);

				if (ImGui::IsItemDeactivatedAfterEdit() && !ShaderInjectorIO::WriteInjectorMenuScale(Globals::gShaderInjectorGUIScale))
					WriteToRuntimeLogError("Could not save MenuScale to ShaderInjector.ini.");

				const float settingsButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

				if (ImGui::Button("Save Settings", ImVec2(settingsButtonWidth, 0)))
				{
					if (ShaderInjectorIO::WriteInjectorSettings())
						WriteToRuntimeLogSuccess("Saved ShaderInjector.ini.");
					else
						WriteToRuntimeLogError("Could not save ShaderInjector.ini.");
				}

				ImGui::SameLine();

				if (ImGui::Button("Open Settings File", ImVec2(settingsButtonWidth, 0)) && !ShaderInjectorIO::OpenFile(ShaderInjectorIO::GetInjectorSettingsPath()))
					WriteToRuntimeLogError("Could not open ShaderInjector.ini.");

				ImGui::Unindent(indentSpace);
			}

			//set in HookD3D12, wires up an event that calls DrawShaderInjectorMenu()
			if (context.drawMenu)
				context.drawMenu();

			//diagnostics stay available in advanced mode.
			if (Globals::gShaderInjectorAdvancedMode)
			{
				if (ImGui::CollapsingHeader("Runtime Log"))
				{
					ImGui::Indent(indentSpace);
					ImGui::Spacing();

					if (ImGui::Button("Clear Log"))
						ClearRuntimeLog();

					ImGui::SameLine();

					if (ImGui::Button("Open Logs Folder") && !ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetLogsDirectory()))
						WriteToRuntimeLogError("Could not open the logs folder.");

					if (context.runtimeLogText)
					{
						const std::string runtimeLogSnapshot = GetRuntimeLogSnapshot();
						ImGui::TextUnformatted(runtimeLogSnapshot.c_str());
					}

					ImGui::Unindent(indentSpace);
				}
			}
		}
		//window end

		ImGui::End();
	}

	void DrawShaderInjectorMenu()
	{
		//reuse the existing editor so edits and pending changes survive switching between views.
		if (!Globals::gShaderInjectorAdvancedMode)
		{
			DrawShaderConfiguration();
			return;
		}

		DrawShaderConfiguration();

		//separate advanced tools from the configuration controls shared by both modes.
		ImGui::Spacing();
		ImGui::SeparatorText("Advanced");

		DrawModifiedShaders();
		DrawShaderTargets();
		DrawShaderPatches();
		DrawShaderResources();
		DrawRenderPasses();
		DrawShaderCompilerSettings();
		DrawShaderInspector();
		DrawRenderDoc();
	}
} //namespace ShaderInjectorGUI
