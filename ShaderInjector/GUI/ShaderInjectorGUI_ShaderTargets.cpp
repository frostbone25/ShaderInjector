//ShaderInjectorGUI.cpp
#include "ShaderInjectorGUI.h"
#include "GUI/ShaderTargetReloadResult.h"

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
	ShaderTargetReloadResult ReloadAllActiveShaderTargets()
	{
		ShaderTargetReloadResult result{};
		DatabaseModifiedShaders::RefreshModifiedShaders();

		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();

		for (int shaderTargetIndex = 0; shaderTargetIndex < static_cast<int>(HookD3D12::gLoadedShaderTargets.size()); ++shaderTargetIndex)
		{
			const ShaderTarget::ShaderTargetDisk& shaderTarget = HookD3D12::gLoadedShaderTargets[shaderTargetIndex];

			if (!HookD3D12::IsShaderTargetEffectivelyEnabled(shaderTarget))
			{
				++result.skippedInactiveShaderTargetCount;
				continue;
			}

			++result.activeShaderTargetCount;

			if (HookD3D12::ReloadShaderTarget(shaderTargetIndex))
				++result.reloadedShaderTargetCount;
		}

		return result;
	}

	bool EnsureModifiedShaderCompileQueuedForShaderTarget(const std::string& modifiedShaderId)
	{
		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);

		if (!modifiedShader)
		{
			WriteToRuntimeLogError("Modified Shader package not found: " + modifiedShaderId);
			return false;
		}

		if (DatabaseModifiedShaders::HasCompiledVariant(*modifiedShader))
			return true;

		//a package with per-target defines has no single blob file to re-read.
		if (!modifiedShader->HasPerTargetDefines() &&
			!modifiedShader->compiledBlobPath.empty() &&
			ShaderInjectorIO::FileExists(modifiedShader->compiledBlobPath))
		{
			DatabaseModifiedShaders::RefreshModifiedShaders();
			modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);

			if (modifiedShader && DatabaseModifiedShaders::HasCompiledVariant(*modifiedShader))
				return true;
		}

		if (!DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId))
		{
			WriteToRuntimeLogError("Failed to queue Modified Shader package compile: " + modifiedShaderId);
			return false;
		}

		WriteToRuntimeLogSuccess("Queued Modified Shader package compile: " + modifiedShaderId);
		return true;
	}

	bool SaveAndReloadShaderTargetAfterModifiedShaderChange(int shaderTargetIndex)
	{
		if (shaderTargetIndex < 0 || shaderTargetIndex >= (int)HookD3D12::gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& shaderTarget = HookD3D12::gLoadedShaderTargets[shaderTargetIndex];

		if (!EnsureModifiedShaderCompileQueuedForShaderTarget(shaderTarget.modifiedShaderId))
			return false;

		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(shaderTarget.modifiedShaderId);

		if (!modifiedShader)
			return false;

		if (!modifiedShader->enabled || modifiedShader->shaderType != shaderTarget.shaderType)
		{
			WriteToRuntimeLogError("Modified Shader package is disabled or has the wrong shader type: " + modifiedShader->id);
			return false;
		}

		shaderTarget.shaderProfile = modifiedShader->shaderProfile;
		shaderTarget.shaderEntryPoint = modifiedShader->shaderEntryPoint;
		shaderTarget.modifiedShaderBlobPath = modifiedShader->compiledBlobPath;

		if (!ShaderTarget::WriteShaderTargetJson(shaderTarget))
		{
			WriteToRuntimeLogError("Failed to save Shader Target after Modified Shader package change: " + shaderTarget.jsonPath);
			return false;
		}

		if (!shaderTarget.enabled)
		{
			HookD3D12::MarkShaderTargetApplyDirty();
			WriteToRuntimeLogWarning("Saved Modified Shader package change, but Shader Target is disabled: " + shaderTarget.name);
			return true;
		}

		const bool reloaded = HookD3D12::ReloadShaderTarget(shaderTargetIndex);

		if (reloaded)
			WriteToRuntimeLogSuccess("Shader Target now uses Modified Shader package: " + DatabaseModifiedShaders::DisplayName(*modifiedShader));
		else
			WriteToRuntimeLogError("Failed to reload Shader Target after Modified Shader package change: " + shaderTarget.name);

		return reloaded;
	}

	void DrawShaderTargets()
	{
		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();

		const std::string shaderTargetsHeader = "Shader Targets: " + std::to_string(HookD3D12::gLoadedShaderTargets.size()) + "###ShaderTargets";

		if (ImGui::CollapsingHeader(shaderTargetsHeader.c_str()))
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			ImGui::InputTextMultiline("##ShaderTargetsNote",
									  const_cast<char*>(noteShaderTargetsText),
									  strlen(noteShaderTargetsText) + 1,
									  ImVec2(-FLT_MIN, 0), //-FLT_MIN width = stretch to window edge, 0 height = auto
									  ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

			if (ImGui::Button("Refresh##ShaderTargets"))
				HookD3D12::RefreshLoadedShaderTargets();

			ImGui::SameLine();

			if (ImGui::Button("Open Folder##ShaderTargets") && !ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetShaderTargetsDirectory()))
				WriteToRuntimeLogError("Could not open Modified Shader package folder: " + ShaderInjectorIO::GetShaderTargetsDirectory());

			ImGui::SameLine();
			ImGui::BeginDisabled(HookD3D12::gLoadedShaderTargets.empty());

			if (ImGui::Button("Reload All##ShaderTargets"))
			{
				const ShaderTargetReloadResult result = ReloadAllActiveShaderTargets();
				const std::string summary =
					"Reload All Shader Targets: reloaded=" + std::to_string(result.reloadedShaderTargetCount) +
					"/" + std::to_string(result.activeShaderTargetCount) +
					" skippedInactiveTargets=" + std::to_string(result.skippedInactiveShaderTargetCount);

				if (result.reloadedShaderTargetCount == result.activeShaderTargetCount)
					WriteToRuntimeLogSuccess(summary);
				else
					WriteToRuntimeLogError(summary);
			}

			ImGui::EndDisabled();

			if (HookD3D12::gLoadedShaderTargets.empty())
			{
				ImGui::Text("No replacement JSON files found.");
				return;
			}

			if (HookD3D12::gSelectedShaderTargetIndex < 0 || HookD3D12::gSelectedShaderTargetIndex >= (int)HookD3D12::gLoadedShaderTargets.size())
				HookD3D12::gSelectedShaderTargetIndex = 0;

			const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;

			if (ImGui::BeginTable("ShaderTargetList##ShaderTargets", 3, tableFlags, ImVec2(0, 180)))
			{
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 2.2f);
				ImGui::TableSetupColumn("Shader Type", ImGuiTableColumnFlags_None, 1.0f);
				ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_NoSort, 0.8f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				std::vector<int> displayedIndices;

				for (int index = 0; index < static_cast<int>(HookD3D12::gLoadedShaderTargets.size()); ++index)
					displayedIndices.push_back(index);

				const ImGuiTableSortSpecs* sortSpecifications = ImGui::TableGetSortSpecs();

				std::stable_sort(displayedIndices.begin(), displayedIndices.end(), [&](int leftIndex, int rightIndex)
				{
					const ShaderTarget::ShaderTargetDisk& leftTarget = HookD3D12::gLoadedShaderTargets[leftIndex];
					const ShaderTarget::ShaderTargetDisk& rightTarget = HookD3D12::gLoadedShaderTargets[rightIndex];

					if (sortSpecifications)
					{
						for (int sortIndex = 0; sortIndex < sortSpecifications->SpecsCount; ++sortIndex)
						{
							const ImGuiTableColumnSortSpecs& specification = sortSpecifications->Specs[sortIndex];
							int order = 0;

							if (specification.ColumnIndex == 0)
							{
								std::string leftName = leftTarget.jsonPath;
								std::string rightName = rightTarget.jsonPath;

								if (!leftTarget.name.empty())
									leftName = leftTarget.name;

								if (!rightTarget.name.empty())
									rightName = rightTarget.name;

								order = leftName.compare(rightName);
							}
							else if (specification.ColumnIndex == 1)
								order = StringHelper::ShaderTypeToString(leftTarget.shaderType).compare(StringHelper::ShaderTypeToString(rightTarget.shaderType));

							if (order == 0)
								continue;

							if (specification.SortDirection == ImGuiSortDirection_Descending)
								return order > 0;

							return order < 0;
						}
					}

					if (leftTarget.jsonPath != rightTarget.jsonPath)
						return leftTarget.jsonPath < rightTarget.jsonPath;

					if (leftTarget.originalShaderBytecodeHash != rightTarget.originalShaderBytecodeHash)
						return leftTarget.originalShaderBytecodeHash < rightTarget.originalShaderBytecodeHash;

					return leftIndex < rightIndex;
				});

				for (int targetIndex : displayedIndices)
				{
					const ShaderTarget::ShaderTargetDisk& replacement = HookD3D12::gLoadedShaderTargets[targetIndex];
					std::string label = replacement.jsonPath;

					if (!replacement.name.empty())
						label = replacement.name;

					if (!replacement.originalShaderBytecodeHash.empty())
						label += " [" + replacement.originalShaderBytecodeHash + "]";

					const bool isSelected = targetIndex == HookD3D12::gSelectedShaderTargetIndex;
					ImGui::PushID(targetIndex);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);

					if (replacement.enabled)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
					}

					if (ImGui::Selectable(label.c_str(), isSelected || replacement.enabled, ImGuiSelectableFlags_SpanAllColumns))
					{
						HookD3D12::gSelectedShaderTargetIndex = targetIndex;
						HookD3D12::gShaderTargetNameBufferIndex = -1;
					}

					if (replacement.enabled)
						ImGui::PopStyleColor(3);

					if (isSelected)
						ImGui::SetItemDefaultFocus();

					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(StringHelper::ShaderTypeToString(replacement.shaderType).c_str());
					ImGui::TableSetColumnIndex(2);

					if (replacement.enabled)
						ImGui::TextUnformatted("Enabled");
					else
						ImGui::TextUnformatted("Disabled");
					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			if (HookD3D12::gSelectedShaderTargetIndex < 0 || HookD3D12::gSelectedShaderTargetIndex >= (int)HookD3D12::gLoadedShaderTargets.size())
				return;

			HookD3D12::SyncShaderTargetNameBuffer();

			ShaderTarget::ShaderTargetDisk& replacement = HookD3D12::gLoadedShaderTargets[HookD3D12::gSelectedShaderTargetIndex];
			size_t loadedBlobSize = 0;

			if (HookD3D12::gSelectedShaderTargetIndex < (int)HookD3D12::gLoadedShaderTargetBlobs.size())
				loadedBlobSize = HookD3D12::gLoadedShaderTargetBlobs[HookD3D12::gSelectedShaderTargetIndex].size();

			//========================== SELECTED ===========================
			ImGui::SeparatorText(HookD3D12::gShaderTargetNameBuffer);
			ImGui::Indent(indentSpace);

			if (ImGui::Button("Refresh##ShaderTargetSourceSection"))
				DatabaseModifiedShaders::RefreshModifiedShaders();

			ImGui::SameLine();

			if (ImGui::Button("Delete##ShaderTarget"))
			{
				HookD3D12::DeleteShaderTarget(HookD3D12::gSelectedShaderTargetIndex);
				ImGui::Unindent(indentSpace);
				return;
			}

			if (PropertyWidgets::Checkbox("Enabled", "##ShaderTargetEnable", &replacement.enabled))
				HookD3D12::MarkShaderTargetApplyDirty();

			ImGui::Unindent(indentSpace);

			DrawShaderTargetSourceSection(replacement, HookD3D12::gSelectedShaderTargetIndex);

			ImGui::Spacing();

			if (ImGui::TreeNodeEx("Info##ShaderTargetInfo"))
			{
				PropertyWidgets::ReadOnlyText("Profile", "##ShaderTargetInfoProfile", replacement.shaderProfile);
				PropertyWidgets::ReadOnlyText("Entry Point", "##ShaderTargetInfoEntryPoint", replacement.shaderEntryPoint);
				PropertyWidgets::ReadOnlyText("Hash", "##ShaderTargetInfoHash", replacement.originalShaderBytecodeHash);
				PropertyWidgets::ReadOnlyText("Bytecode Length", "##ShaderTargetInfoBytecodeLength", replacement.originalShaderBytecodeLength);
				PropertyWidgets::ReadOnlyText("Loaded Modified Blob Bytes", "##ShaderTargetInfoLoadedBytes", std::to_string(loadedBlobSize));
				PropertyWidgets::ReadOnlyText("Source List", "##ShaderTargetInfoSourceList", replacement.sourceList);
				PropertyWidgets::ReadOnlyText("Pipeline Index", "##ShaderTargetInfoPipelineIndex", replacement.pipelineIndex);
				PropertyWidgets::ReadOnlyText("JSON", "##ShaderTargetInfoJSON", replacement.jsonPath);
				PropertyWidgets::ReadOnlyText("Original Blob", "##ShaderTargetInfoOriginalBlob", replacement.originalShaderBlobPath);
				PropertyWidgets::ReadOnlyText("Modified Shader", "##ShaderTargetInfoModifiedShader", replacement.modifiedShaderId);
				ImGui::Separator();
				ImGui::TreePop();
			}

			ImGui::Spacing();

			DrawShaderTargetPSOList(replacement);

			ImGui::Spacing();

			ImGui::Unindent(indentSpace);
		}
	}

	void DrawShaderTargetSourceSection(ShaderTarget::ShaderTargetDisk& replacement, int replacementIndex)
	{
		DatabaseModifiedShaders::EnsureModifiedShadersLoaded();
		ImGui::Indent(indentSpace);

		PropertyWidgets::ReadOnlyText("Shader Type", "##ShaderTargetSourceShaderType", StringHelper::ShaderTypeToString(replacement.shaderType).c_str());
		PropertyWidgets::ReadOnlyText("Package Folder", "##ShaderTargetSourcePackageFolder", ShaderInjectorIO::GetModifiedShadersDirectory());

		const ModifiedShader::ModifiedShaderPackageDisk* selectedPackage = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		std::string currentPackageName = "(none)";

		if (selectedPackage)
			currentPackageName = DatabaseModifiedShaders::DisplayName(*selectedPackage);

		if (PropertyWidgets::BeginCombo("Modified Shader Package", "##ShaderTargetModifiedShader", currentPackageName.c_str()))
		{
			for (const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader : DatabaseModifiedShaders::GetModifiedShaders())
			{
				if (!modifiedShader.enabled || modifiedShader.shaderType != replacement.shaderType)
					continue;

				const bool selected = modifiedShader.id == replacement.modifiedShaderId;
				const std::string displayName = DatabaseModifiedShaders::DisplayName(modifiedShader);

				if (ImGui::Selectable(displayName.c_str(), selected))
				{
					if (!selected)
					{
						replacement.modifiedShaderId = modifiedShader.id;
						replacement.shaderProfile = modifiedShader.shaderProfile;
						replacement.shaderEntryPoint = modifiedShader.shaderEntryPoint;
						replacement.modifiedShaderBlobPath = modifiedShader.compiledBlobPath;

						SaveAndReloadShaderTargetAfterModifiedShaderChange(replacementIndex);
					}
				}

				if (selected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		selectedPackage = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		ImGui::Unindent(indentSpace);
	}

	void DrawShaderTargetPSOList(const ShaderTarget::ShaderTargetDisk& replacement)
	{
		const int replacementCount = CountReplacementPSOs(replacement);
		std::string title = "Replacing Active PSOs: " + std::to_string(replacementCount);

		if (ImGui::TreeNodeEx(title.c_str()))
		{
			if (replacementCount == 0)
			{
				ImGui::Text("No active PSOs are currently using this replacement.");
				//the empty branch still owns the tree scope opened above.
				ImGui::TreePop();
				return;
			}

			if (ImGui::BeginChild("ReplacementPSOList##SelectedShaderTarget", ImVec2(0, 130), ImGuiChildFlags_Borders))
			{
				for (int i = 0; i < (int)HookD3D12::gGraphicsPipelines.size(); i++)
				{
					const auto& pipeline = HookD3D12::gGraphicsPipelines[i];

					if (PipelineUsesReplacement(pipeline, replacement))
						DrawReplacementPSORow("Graphics", i, pipeline);
				}

				for (int i = 0; i < (int)HookD3D12::gPipelineStates.size(); i++)
				{
					const auto& pipeline = HookD3D12::gPipelineStates[i];

					if (PipelineUsesReplacement(pipeline, replacement))
						DrawReplacementPSORow("Stream", i, pipeline);
				}
			}

			ImGui::EndChild();
			ImGui::TreePop();
		}
	}

	template <typename PipelineT>
	void DrawReplacementPSORow(const char* sourceList, int index, const PipelineT& pipeline)
	{
		ImGui::Text("%s #%d", sourceList, index);
		ImGui::SameLine();
		ImGui::Text("Original: %p", pipeline.pipelineState);
		ImGui::Text("Replacement: %p", pipeline.pipelineStateWithReplacement);
		ImGui::SameLine();
		ImGui::Text("Type: %s", StringHelper::ShaderTypeToString(pipeline.activeShaderTargetType).c_str());
		ImGui::Text("Hash: %s", Hash::FormatHash(pipeline.activeShaderTargetHash).c_str());

		if (pipeline.activeShaderTargetUsesFallback)
		{
			ImGui::SameLine();
			ImGui::Text("(fallback)");
		}

		ImGui::Separator();
	}

	int CountReplacementPSOs(const ShaderTarget::ShaderTargetDisk& replacement)
	{
		int count = 0;

		for (const auto& pipeline : HookD3D12::gGraphicsPipelines)
		{
			if (PipelineUsesReplacement(pipeline, replacement))
				count++;
		}

		for (const auto& pipeline : HookD3D12::gPipelineStates)
		{
			if (PipelineUsesReplacement(pipeline, replacement))
				count++;
		}

		return count;
	}

	template <typename PipelineT>
	bool PipelineUsesReplacement(const PipelineT& pipeline, const ShaderTarget::ShaderTargetDisk& replacement)
	{
		if (!HookD3D12::IsShaderTargetEffectivelyEnabled(replacement) || !pipeline.pipelineStateWithReplacement)
			return false;

		return pipeline.activeShaderTargetType == replacement.shaderType && pipeline.activeShaderTargetHash == Hash::ParseHashText(replacement.originalShaderBytecodeHash);
	}
} //namespace ShaderInjectorGUI
