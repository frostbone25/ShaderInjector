//ShaderInjectorGUI.cpp
#include "ShaderInjectorGUI.h"
#include "GUI/ModifiedShaderRecompileResult.h"

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
#include "GUI/PropertyWidgets.h"
#include "Input/Keycodes.h"
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

	ModifiedShaderRecompileResult RecompileModifiedShaderAndReloadLinkedTargets(const std::string& modifiedShaderId)
	{
		ModifiedShaderRecompileResult result{};
		result.compileQueued = DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId);

		if (!result.compileQueued)
			return result;

		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);

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

			++result.pendingReloadShaderTargetCount;
		}

		return result;
	}

	static void DrawModifiedShaderCompileProgress()
	{
		const DatabaseModifiedShaders::CompileProgress progress = DatabaseModifiedShaders::GetCompileProgress();

		if (progress.total == 0 || progress.completed >= progress.total)
			return;

		std::string overlay = "Compiling shaders " + std::to_string(progress.completed) + "/" + std::to_string(progress.total);

		if (progress.failed > 0)
			overlay += " (" + std::to_string(progress.failed) + " failed)";

		ImGui::ProgressBar(static_cast<float>(progress.completed) / static_cast<float>(progress.total), ImVec2(-FLT_MIN, 0), overlay.c_str());

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"Shaders in use compile first. Current replacements stay active until their new version is ready.\n"
				"running=%d queued=%d",
				progress.running,
				progress.queued);
		}
	}

	ModifiedShaderBatchRecompileResult RecompileModifiedShaders(bool includeInactivePackages)
	{
		ModifiedShaderBatchRecompileResult batchResult{};
		const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& modifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();
		std::vector<std::string> selectedModifiedShaderIds;
		selectedModifiedShaderIds.reserve(modifiedShaders.size());

		for (const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader : modifiedShaders)
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

			if (!result.compileQueued)
			{
				++batchResult.failedPackageCount;
				WriteToRuntimeLogError("Failed to queue Modified Shader compile: " + modifiedShaderId);
				continue;
			}

			++batchResult.queuedPackageCount;
			batchResult.linkedShaderTargetCount += result.linkedShaderTargetCount;
			batchResult.pendingReloadShaderTargetCount += result.pendingReloadShaderTargetCount;
			batchResult.skippedInactiveShaderTargetCount += result.skippedInactiveShaderTargetCount;
		}

		std::string actionName = "Recompile All Active Modified Shaders";

		if (includeInactivePackages)
			actionName = "Force Recompile All Modified Shaders";

		const std::string summary =
			actionName +
			": queued=" + std::to_string(batchResult.queuedPackageCount) +
			"/" + std::to_string(batchResult.selectedPackageCount) +
			" failed=" + std::to_string(batchResult.failedPackageCount) +
			" skippedInactivePackages=" +
			std::to_string(batchResult.skippedInactivePackageCount) +
			" targetsToReload=" +
			std::to_string(batchResult.pendingReloadShaderTargetCount) +
			"/" + std::to_string(batchResult.linkedShaderTargetCount) +
			" skippedInactiveTargets=" +
			std::to_string(batchResult.skippedInactiveShaderTargetCount);

		if (batchResult.failedPackageCount == 0 &&
			batchResult.pendingReloadShaderTargetCount + batchResult.skippedInactiveShaderTargetCount == batchResult.linkedShaderTargetCount)
		{
			WriteToRuntimeLogSuccess(summary);
		}
		else
		{
			WriteToRuntimeLogError(summary);
		}

		return batchResult;
	}

	void DrawModifiedShaders()
	{
		DatabaseModifiedShaders::EnsureModifiedShadersLoaded();
		const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& modifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();
		const std::string modifiedShadersHeader = "Modified Shaders: " + std::to_string(modifiedShaders.size()) + "###ModifiedShaders";

		const bool modifiedShadersOpen = ImGui::CollapsingHeader(modifiedShadersHeader.c_str());

		if (modifiedShadersOpen)
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			DrawModifiedShaderCompileProgress();

			ImGui::InputTextMultiline("##ModifiedShadersNote",
									  const_cast<char*>(noteModifiedShadersText),
									  strlen(noteModifiedShadersText) + 1,
									  ImVec2(-FLT_MIN, 0), //-FLT_MIN width = stretch to window edge, 0 height = auto
									  ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

			if (!HookD3D12::gLoadedShaderTargetsOnce)
				HookD3D12::RefreshLoadedShaderTargets();

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

			const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& refreshedModifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();

			if (refreshedModifiedShaders.empty())
			{
				ImGui::Text("No Modified Shader packages found.");
				ImGui::Unindent(indentSpace);
				return;
			}

			if (gSelectedModifiedShaderId.empty() || !DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId))
				gSelectedModifiedShaderId = refreshedModifiedShaders.front().id;

			const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;

			if (ImGui::BeginTable("ModifiedShaders##ModifiedShadersList", 4, tableFlags, ImVec2(0, 180)))
			{
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 2.4f);
				ImGui::TableSetupColumn("Shader Type", ImGuiTableColumnFlags_None, 1.0f);
				ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_NoSort, 1.0f);
				ImGui::TableSetupColumn("Targets", ImGuiTableColumnFlags_NoSort, 0.7f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				std::vector<size_t> displayedIndices;

				for (size_t index = 0; index < refreshedModifiedShaders.size(); ++index)
					displayedIndices.push_back(index);

				const ImGuiTableSortSpecs* sortSpecifications = ImGui::TableGetSortSpecs();

				std::stable_sort(displayedIndices.begin(), displayedIndices.end(), [&](size_t leftIndex, size_t rightIndex)
				{
					const ModifiedShader::ModifiedShaderPackageDisk& leftShader = refreshedModifiedShaders[leftIndex];
					const ModifiedShader::ModifiedShaderPackageDisk& rightShader = refreshedModifiedShaders[rightIndex];

					if (sortSpecifications)
					{
						for (int sortIndex = 0; sortIndex < sortSpecifications->SpecsCount; ++sortIndex)
						{
							const ImGuiTableColumnSortSpecs& specification = sortSpecifications->Specs[sortIndex];
							int order = 0;

							if (specification.ColumnIndex == 0)
								order = DatabaseModifiedShaders::DisplayName(leftShader).compare(DatabaseModifiedShaders::DisplayName(rightShader));
							else if (specification.ColumnIndex == 1)
								order = StringHelper::ShaderTypeToString(leftShader.shaderType).compare(StringHelper::ShaderTypeToString(rightShader.shaderType));

							if (order == 0)
								continue;

							if (specification.SortDirection == ImGuiSortDirection_Descending)
								return order > 0;

							return order < 0;
						}
					}

					return leftShader.id < rightShader.id;
				});

				for (size_t index : displayedIndices)
				{
					const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader = refreshedModifiedShaders[index];
					const bool isUsedByShaderTarget = ModifiedShaderIsUsedByEnabledShaderTarget(modifiedShader.id);
					const bool isActiveUsedPackage = modifiedShader.enabled && isUsedByShaderTarget;
					const bool isSelected = modifiedShader.id == gSelectedModifiedShaderId;
					int linkedTargetCount = 0;

					for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
						if (shaderTarget.modifiedShaderId == modifiedShader.id)
							++linkedTargetCount;

					std::string status = "Active";

					if (!modifiedShader.enabled && !isUsedByShaderTarget)
						status = "Disabled; not used";
					else if (!modifiedShader.enabled)
						status = "Disabled";
					else if (!isUsedByShaderTarget)
						status = "Not used";

					ImGui::PushID(modifiedShader.id.c_str());
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);

					if (isActiveUsedPackage)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
					}

					const bool rowSelected = ImGui::Selectable(DatabaseModifiedShaders::DisplayName(modifiedShader).c_str(), isSelected || isActiveUsedPackage, ImGuiSelectableFlags_SpanAllColumns);

					if (rowSelected)
						gSelectedModifiedShaderId = modifiedShader.id;

					if (isActiveUsedPackage)
						ImGui::PopStyleColor(3);

					if (isSelected)
						ImGui::SetItemDefaultFocus();

					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(StringHelper::ShaderTypeToString(modifiedShader.shaderType).c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(status.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%d", linkedTargetCount);
					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			const ModifiedShader::ModifiedShaderPackageDisk* selectedModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId);

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

			const bool saveNameFromButton = ImGui::Button("Save Name");

			ImGui::SameLine();

			if (ImGui::Button("Recompile##ModifiedShaders"))
			{
				const ModifiedShaderRecompileResult result = RecompileModifiedShaderAndReloadLinkedTargets(gSelectedModifiedShaderId);

				if (!result.compileQueued)
				{
					WriteToRuntimeLogError("Failed to queue Modified Shader compile: " + gSelectedModifiedShaderId);
				}
				else
				{
					const int activeLinkedShaderTargetCount = result.linkedShaderTargetCount - result.skippedInactiveShaderTargetCount;

					if (activeLinkedShaderTargetCount == 0)
						WriteToRuntimeLogSuccess("Queued Modified Shader compile: " + gSelectedModifiedShaderId);
					else
						WriteToRuntimeLogSuccess("Queued Modified Shader compile; " + std::to_string(activeLinkedShaderTargetCount) + " active linked replacements update when ready: " + gSelectedModifiedShaderId);
				}
			}
			selectedModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId);

			ImGui::SameLine();

			if (ImGui::Button("Recapture##ModifiedShader"))
			{
				std::string recaptureMessage;
				if (DatabaseModifiedShaders::RecaptureModifiedShader(gSelectedModifiedShaderId, recaptureMessage))
					WriteToRuntimeLogSuccess(recaptureMessage);
				else
					WriteToRuntimeLogError(recaptureMessage);
				selectedModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId);
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Refresh the original shader fingerprint and linked PSO data. Preserve the source HLSL and compiled replacement bytecode.");

			ImGui::SameLine();

			if (ImGui::Button("Open Folder##ModifiedShaderSelected") && !ShaderInjectorIO::OpenDirectory(selectedModifiedShader->packageDirectory))
				WriteToRuntimeLogError("Could not open Modified Shader package folder: " + selectedModifiedShader->packageDirectory);

			ImGui::SameLine();

			if (ImGui::Button("Delete##ModifiedShader"))
				ImGui::OpenPopup("Delete Modified Shader?");

			bool enabled = selectedModifiedShader->enabled;

			if (PropertyWidgets::Checkbox("Enabled", "##ModifiedShaderEnable", &enabled))
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

			const bool saveNameFromEnter = PropertyWidgets::InputText(
				"Name",
				"##ModifiedShaderName",
				gModifiedShaderNameBuffer,
				sizeof(gModifiedShaderNameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue);

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

			if (saveNameFromEnter || saveNameFromButton)
			{
				if (DatabaseModifiedShaders::SetModifiedShaderName(gSelectedModifiedShaderId, gModifiedShaderNameBuffer))
					WriteToRuntimeLogSuccess("Saved Modified Shader name: " + std::string(gModifiedShaderNameBuffer));
				else
					WriteToRuntimeLogError("Failed to save Modified Shader name. The name cannot be empty.");

				selectedModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(gSelectedModifiedShaderId);
			}

			if (!selectedModifiedShader)
			{
				ImGui::Unindent(indentSpace);
				ImGui::Unindent(indentSpace);
				return;
			}

			PropertyWidgets::ReadOnlyText("Shader Type", "##ModifiedShaderType", StringHelper::ShaderTypeToString(selectedModifiedShader->shaderType).c_str());
			PropertyWidgets::ReadOnlyText("Profile", "##ModifiedShaderProfile", selectedModifiedShader->shaderProfile);
			PropertyWidgets::ReadOnlyText("Entry Point", "##ModifiedShaderEntryPoint", selectedModifiedShader->shaderEntryPoint);
			PropertyWidgets::ReadOnlyText("Source", "##ModifiedShaderSource", selectedModifiedShader->sourcePath);
			PropertyWidgets::ReadOnlyText("Compiled Blob", "##ModifiedShaderCompiledBlob", selectedModifiedShader->compiledBlobPath);
			ImGui::Spacing();
			ImGui::Unindent(indentSpace);
			ImGui::Unindent(indentSpace);
		}
	}
} //namespace ShaderInjectorGUI
