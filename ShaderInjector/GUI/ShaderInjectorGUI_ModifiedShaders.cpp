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
	static std::string gSelectedModifiedShaderId;
	static std::string gModifiedShaderNameBufferId;
	static char gModifiedShaderNameBuffer[256]{};

	bool ModifiedShaderIsUsedByEnabledShaderTarget(const std::string& modifiedShaderId)
	{
		if (modifiedShaderId.empty())
			return false;

		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();

		for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
		{
			if (shaderTarget.enabled && shaderTarget.modifiedShaderId == modifiedShaderId)
				return true;
		}

		return false;
	}

	struct ModifiedShaderRecompileResult
	{
		bool compiled = false;
		int linkedShaderTargetCount = 0;
		int reloadedShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};

	ModifiedShaderRecompileResult RecompileModifiedShaderAndReloadLinkedTargets(const std::string& modifiedShaderId)
	{
		ModifiedShaderRecompileResult result{};
		result.compiled = DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId);

		if (!result.compiled)
			return result;

		const ModifiedShader::PackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);

		if (!modifiedShader)
			return result;

		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();

		for (int shaderTargetIndex = 0; shaderTargetIndex < static_cast<int>(HookD3D12::gLoadedShaderTargets.size()); ++shaderTargetIndex)
		{
			const ShaderTarget::ShaderTargetDisk& shaderTarget = HookD3D12::gLoadedShaderTargets[shaderTargetIndex];

			if (shaderTarget.modifiedShaderId != modifiedShaderId)
				continue;

			++result.linkedShaderTargetCount;

			if (!shaderTarget.enabled || !modifiedShader->enabled || shaderTarget.shaderType != modifiedShader->shaderType)
			{
				++result.skippedInactiveShaderTargetCount;
				continue;
			}

			if (HookD3D12::ReloadShaderTarget(shaderTargetIndex))
				++result.reloadedShaderTargetCount;
		}

		return result;
	}

	ModifiedShaderBatchRecompileResult RecompileModifiedShaders(bool includeInactivePackages)
	{
		ModifiedShaderBatchRecompileResult batchResult{};
		const std::vector<ModifiedShader::PackageDisk>& modifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();
		std::vector<std::string> selectedModifiedShaderIds;
		selectedModifiedShaderIds.reserve(modifiedShaders.size());

		for (const ModifiedShader::PackageDisk& modifiedShader : modifiedShaders)
		{
			const bool active = modifiedShader.enabled && ModifiedShaderIsUsedByEnabledShaderTarget(modifiedShader.id);

			if (!includeInactivePackages && !active)
			{
				++batchResult.skippedInactivePackageCount;
				continue;
			}

			selectedModifiedShaderIds.push_back(modifiedShader.id);
		}

		batchResult.selectedPackageCount = static_cast<int>(selectedModifiedShaderIds.size());

		for (const std::string& modifiedShaderId : selectedModifiedShaderIds)
		{
			const ModifiedShaderRecompileResult result = RecompileModifiedShaderAndReloadLinkedTargets(modifiedShaderId);

			if (!result.compiled)
			{
				++batchResult.failedPackageCount;
				WriteToRuntimeLogError("Failed to compile Modified Shader: " + modifiedShaderId);
				continue;
			}

			++batchResult.compiledPackageCount;
			batchResult.linkedShaderTargetCount += result.linkedShaderTargetCount;
			batchResult.reloadedShaderTargetCount += result.reloadedShaderTargetCount;
			batchResult.skippedInactiveShaderTargetCount += result.skippedInactiveShaderTargetCount;
		}

		const std::string actionName = includeInactivePackages
			? "Force Recompile All Modified Shaders"
			: "Recompile All Active Modified Shaders";

		const std::string summary =
			actionName +
			": compiled=" + std::to_string(batchResult.compiledPackageCount) +
			"/" + std::to_string(batchResult.selectedPackageCount) +
			" failed=" + std::to_string(batchResult.failedPackageCount) +
			" skippedInactivePackages=" +
			std::to_string(batchResult.skippedInactivePackageCount) +
			" reloadedTargets=" +
			std::to_string(batchResult.reloadedShaderTargetCount) +
			"/" + std::to_string(batchResult.linkedShaderTargetCount) +
			" skippedInactiveTargets=" +
			std::to_string(batchResult.skippedInactiveShaderTargetCount);

		if (batchResult.failedPackageCount == 0 &&
			batchResult.reloadedShaderTargetCount +
			batchResult.skippedInactiveShaderTargetCount ==
			batchResult.linkedShaderTargetCount)
		{
			WriteToRuntimeLogSuccess(summary);
		}
		else
		{
			WriteToRuntimeLogError(summary);
		}

		return batchResult;
	}


	void UI_ModifiedShaders()
	{
		if (ImGui::CollapsingHeader("Modified Shaders"))
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			ImGui::InputTextMultiline("##ModifiedShadersNote",
				const_cast<char*>(noteModifiedShadersText),
				strlen(noteModifiedShadersText) + 1,
				ImVec2(-FLT_MIN, 0), // -FLT_MIN width = stretch to window edge, 0 height = auto
				ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap
			);

			DatabaseModifiedShaders::EnsureModifiedShadersLoaded();

			if (!HookD3D12::gLoadedShaderTargetsOnce)
				HookD3D12::RefreshLoadedShaderTargets();

			const std::vector<ModifiedShader::PackageDisk>& modifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();

			ImGui::Text("Loaded: %zu", modifiedShaders.size());
			ImGui::SameLine();

			if (ImGui::Button("Refresh##ModifiedShaders"))
			{
				DatabaseModifiedShaders::RefreshModifiedShaders();

				if (!gSelectedModifiedShaderId.empty() && !DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId))
					gSelectedModifiedShaderId.clear();
			}

			ImGui::SameLine();

			if (ImGui::Button("Open Folder##ModifiedShaders"))
			{
				if (!ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetModifiedShadersDirectory()))
					WriteToRuntimeLogError("Could not open the Modified Shaders folder.");
			}

			ImGui::SameLine();
			ImGui::BeginDisabled(modifiedShaders.empty());

			if (ImGui::Button("Recompile All##ModifiedShaders"))
				RecompileModifiedShaders(false);

			ImGui::SameLine();

			if (ImGui::Button("Force Recompile All##ModifiedShaders"))
				RecompileModifiedShaders(true);

			ImGui::EndDisabled();

			const std::vector<ModifiedShader::PackageDisk>& refreshedModifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();

			if (refreshedModifiedShaders.empty())
			{
				ImGui::Text("No Modified Shader packages found.");
				ImGui::Unindent(indentSpace);
				return;
			}

			if (gSelectedModifiedShaderId.empty() || !DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId))
				gSelectedModifiedShaderId = refreshedModifiedShaders.front().id;

			if (ImGui::BeginChild("ModifiedShaders##ModifiedShadersList", ImVec2(0, 180), ImGuiChildFlags_Borders))
			{
				for (size_t index = 0; index < refreshedModifiedShaders.size(); ++index)
				{
					const ModifiedShader::PackageDisk& modifiedShader = refreshedModifiedShaders[index];
					const bool isUsedByShaderTarget = ModifiedShaderIsUsedByEnabledShaderTarget(modifiedShader.id);
					const bool isActiveUsedPackage = modifiedShader.enabled && isUsedByShaderTarget;
					std::string label = DatabaseModifiedShaders::DisplayName(modifiedShader);

					if (!modifiedShader.enabled)
						label += " (disabled)";

					if (!isUsedByShaderTarget)
						label += " (not used)";

					label += "##ModifiedShader" + std::to_string(index);
					const bool isSelected = modifiedShader.id == gSelectedModifiedShaderId;

					if (isActiveUsedPackage)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
					}

					if (ImGui::Selectable(label.c_str(), isSelected || isActiveUsedPackage))
						gSelectedModifiedShaderId = modifiedShader.id;

					if (isActiveUsedPackage)
						ImGui::PopStyleColor(3);

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndChild();

			const ModifiedShader::PackageDisk* selectedModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId);

			if (!selectedModifiedShader)
			{
				ImGui::Unindent(indentSpace);
				return;
			}

			ImGui::SeparatorText(DatabaseModifiedShaders::DisplayName(*selectedModifiedShader).c_str());
			ImGui::Indent(indentSpace);

			if (gModifiedShaderNameBufferId != selectedModifiedShader->id)
			{
				gModifiedShaderNameBufferId = selectedModifiedShader->id;
				std::snprintf(
					gModifiedShaderNameBuffer,
					sizeof(gModifiedShaderNameBuffer),
					"%s",
					selectedModifiedShader->name.c_str());
			}

			ImGui::Text("Name");
			ImGui::SameLine();

			const float saveNameButtonWidth = ImGui::CalcTextSize("Save Name").x + ImGui::GetStyle().FramePadding.x * 2.0f;

			ImGui::SetNextItemWidth((std::max)(80.0f, ImGui::GetContentRegionAvail().x - saveNameButtonWidth - ImGui::GetStyle().ItemSpacing.x));

			const bool saveNameFromEnter = ImGui::InputText(
				"##ModifiedShaderName",
				gModifiedShaderNameBuffer,
				sizeof(gModifiedShaderNameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue);

			ImGui::SameLine();

			const bool saveNameFromButton = ImGui::Button("Save Name");

			if (saveNameFromEnter || saveNameFromButton)
			{
				if (DatabaseModifiedShaders::SetModifiedShaderName(gSelectedModifiedShaderId, gModifiedShaderNameBuffer))
					WriteToRuntimeLogSuccess("Saved Modified Shader name: " + std::string(gModifiedShaderNameBuffer));
				else
					WriteToRuntimeLogError("Failed to save Modified Shader name. The name cannot be empty.");

				selectedModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId);
			}

			ImGui::Text("Enabled");

			ImGui::SameLine();

			bool enabled = selectedModifiedShader->enabled;

			if (ImGui::Checkbox("##ModifiedShaderEnable", &enabled))
			{
				if (!DatabaseModifiedShaders::SetModifiedShaderEnabled(gSelectedModifiedShaderId, enabled))
					WriteToRuntimeLogError("Failed to save Modified Shader enabled state.");
				else
					HookD3D12::RefreshShaderTargetsForModifiedShaderStateChange();

				selectedModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId);
			}

			if (!selectedModifiedShader)
			{
				ImGui::Unindent(indentSpace);
				ImGui::Unindent(indentSpace);
				return;
			}

			ImGui::SameLine();

			if (ImGui::Button("Recompile##ModifiedShaders"))
			{
				const ModifiedShaderRecompileResult result = RecompileModifiedShaderAndReloadLinkedTargets(gSelectedModifiedShaderId);

				if (!result.compiled)
				{
					WriteToRuntimeLogError("Failed to compile Modified Shader: " + gSelectedModifiedShaderId);
				}
				else
				{
					const int activeLinkedShaderTargetCount = result.linkedShaderTargetCount - result.skippedInactiveShaderTargetCount;

					if (result.linkedShaderTargetCount == 0)
						WriteToRuntimeLogSuccess("Compiled Modified Shader: " + gSelectedModifiedShaderId);
					else if (activeLinkedShaderTargetCount == 0)
						WriteToRuntimeLogSuccess("Compiled Modified Shader; linked shader targets are inactive: " + gSelectedModifiedShaderId);
					else if (result.reloadedShaderTargetCount == activeLinkedShaderTargetCount)
						WriteToRuntimeLogSuccess("Compiled Modified Shader and reloaded all linked replacements: " + gSelectedModifiedShaderId);
					else
						WriteToRuntimeLogError("Reloaded " + std::to_string(result.reloadedShaderTargetCount) + " of " + std::to_string(activeLinkedShaderTargetCount) + " active linked replacements.");
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Open Folder##ModifiedShaderSelected") && !ShaderInjectorIO::OpenDirectory(selectedModifiedShader->packageDirectory))
			{
				WriteToRuntimeLogError("Could not open Modified Shader package folder: " + selectedModifiedShader->packageDirectory);
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete##ModifiedShader"))
				ImGui::OpenPopup("Delete Modified Shader?");


			bool deletedModifiedShader = false;

			if (ImGui::BeginPopupModal("Delete Modified Shader?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				int linkedReplacementCount = 0;

				for (const ShaderTarget::ShaderTargetDisk& replacement : HookD3D12::gLoadedShaderTargets)
				{
					if (replacement.modifiedShaderId == gSelectedModifiedShaderId)
						++linkedReplacementCount;
				}

				ImGui::Text("Delete '%s'?", selectedModifiedShader->name.c_str());
				ImGui::Text("This permanently removes the package folder and all files inside it.");

				if (linkedReplacementCount > 0)
					ImGui::Text("%d Shader Replacement(s) will retain an unresolved reference.", linkedReplacementCount);

				ImGui::Spacing();

				if (ImGui::Button("Delete Package"))
				{
					const std::string deletedName = selectedModifiedShader->name;

					if (DatabaseModifiedShaders::DeleteModifiedShader(gSelectedModifiedShaderId))
					{
						WriteToRuntimeLogSuccess("Deleted Modified Shader: " + deletedName);
						gSelectedModifiedShaderId.clear();
						gModifiedShaderNameBufferId.clear();
						deletedModifiedShader = true;
						ImGui::CloseCurrentPopup();
					}
					else
					{
						WriteToRuntimeLogError("Failed to delete Modified Shader package: " + deletedName);
					}
				}

				ImGui::SameLine();

				if (ImGui::Button("Cancel"))
					ImGui::CloseCurrentPopup();

				ImGui::EndPopup();
			}

			if (deletedModifiedShader)
			{
				ImGui::Unindent(indentSpace);
				ImGui::Unindent(indentSpace);
				return;
			}

			ImGui::Spacing();
			ImGui::Text("Shader Type: %s", StringHelper::ShaderTypeToString(selectedModifiedShader->shaderType).c_str());
			ImGui::Text("Profile: %s", selectedModifiedShader->shaderProfile.c_str());
			ImGui::Text("Entry Point: %s", selectedModifiedShader->shaderEntryPoint.c_str());
			ImGui::Text("Source: %s", selectedModifiedShader->sourcePath.c_str());
			ImGui::Text("Compiled Blob: %s", selectedModifiedShader->compiledBlobPath.c_str());
			ImGui::Spacing();
			ImGui::Unindent(indentSpace);
			ImGui::Unindent(indentSpace);
		}
	}
}
