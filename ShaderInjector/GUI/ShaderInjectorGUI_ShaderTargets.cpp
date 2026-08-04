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
	struct ShaderTargetReloadResult
	{
		int activeShaderTargetCount = 0;
		int reloadedShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};

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

	bool EnsureModifiedShaderCompiledForShaderTarget(const std::string& modifiedShaderId)
	{
		const ModifiedShader::PackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);

		if (!modifiedShader)
		{
			WriteToRuntimeLogError("Modified Shader package not found: " + modifiedShaderId);
			return false;
		}

		if (!modifiedShader->compiledBlob.empty())
			return true;

		if (!modifiedShader->compiledBlobPath.empty() && ShaderInjectorIO::FileExists(modifiedShader->compiledBlobPath))
		{
			DatabaseModifiedShaders::RefreshModifiedShaders();
			modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);

			if (modifiedShader && !modifiedShader->compiledBlob.empty())
				return true;
		}

		if (!DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId))
		{
			WriteToRuntimeLogError("Failed to compile Modified Shader package: " + modifiedShaderId);
			return false;
		}

		WriteToRuntimeLogSuccess("Compiled Modified Shader package: " + modifiedShaderId);
		return true;
	}

	bool SaveAndReloadShaderTargetAfterModifiedShaderChange(int shaderTargetIndex)
	{
		if (shaderTargetIndex < 0 || shaderTargetIndex >= (int)HookD3D12::gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& shaderTarget = HookD3D12::gLoadedShaderTargets[shaderTargetIndex];

		if (!EnsureModifiedShaderCompiledForShaderTarget(shaderTarget.modifiedShaderId))
			return false;

		const ModifiedShader::PackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(shaderTarget.modifiedShaderId);

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

	void UI_ShaderTargets()
	{
		if (ImGui::CollapsingHeader("Shader Targets"))
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			ImGui::InputTextMultiline("##ShaderTargetsNote",
				const_cast<char*>(noteShaderTargetsText),
				strlen(noteShaderTargetsText) + 1,
				ImVec2(-FLT_MIN, 0), // -FLT_MIN width = stretch to window edge, 0 height = auto
				ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap
			);

			if (!HookD3D12::gLoadedShaderTargetsOnce)
				HookD3D12::RefreshLoadedShaderTargets();

			ImGui::Text("Loaded: %zu", HookD3D12::gLoadedShaderTargets.size());

			ImGui::SameLine();

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

			std::string childLabel = "ShaderTargetList##ShaderTargets";

			if (ImGui::BeginChild(childLabel.c_str(), ImVec2(0, 180), ImGuiChildFlags_Borders))
			{
				for (int i = 0; i < (int)HookD3D12::gLoadedShaderTargets.size(); i++)
				{
					const ShaderTarget::ShaderTargetDisk& replacement = HookD3D12::gLoadedShaderTargets[i];
					std::string label = replacement.name.empty() ? replacement.jsonPath : replacement.name;

					if (!replacement.originalShaderBytecodeHash.empty())
						label += " [" + replacement.originalShaderBytecodeHash + "]";

					if (!replacement.enabled)
						label += " (disabled)";

					label += "##ShaderTarget" + std::to_string(i);

					const bool isSelected = i == HookD3D12::gSelectedShaderTargetIndex;

					if (replacement.enabled)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
					}

					if (ImGui::Selectable(label.c_str(), isSelected || replacement.enabled))
					{
						HookD3D12::gSelectedShaderTargetIndex = i;
						HookD3D12::gShaderTargetNameBufferIndex = -1;
					}

					if (replacement.enabled)
						ImGui::PopStyleColor(3);

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndChild();

			if (HookD3D12::gSelectedShaderTargetIndex < 0 || HookD3D12::gSelectedShaderTargetIndex >= (int)HookD3D12::gLoadedShaderTargets.size())
				return;

			HookD3D12::SyncShaderTargetNameBuffer();

			ShaderTarget::ShaderTargetDisk& replacement = HookD3D12::gLoadedShaderTargets[HookD3D12::gSelectedShaderTargetIndex];
			const size_t loadedBlobSize = HookD3D12::gSelectedShaderTargetIndex < (int)HookD3D12::gLoadedShaderTargetBlobs.size()
				? HookD3D12::gLoadedShaderTargetBlobs[HookD3D12::gSelectedShaderTargetIndex].size()
				: 0;

			//========================== SELECTED ===========================
			ImGui::SeparatorText(HookD3D12::gShaderTargetNameBuffer);
			ImGui::Indent(indentSpace);

			ImGui::Text("Enabled");
			ImGui::SameLine();
			if (ImGui::Checkbox("##ShaderTargetEnable", &replacement.enabled))
				HookD3D12::MarkShaderTargetApplyDirty();
			ImGui::SameLine();

			if (ImGui::Button("Delete##ShaderTarget"))
			{
				HookD3D12::DeleteShaderTarget(HookD3D12::gSelectedShaderTargetIndex);
				return;
			}

			ImGui::Spacing();

			ImGui::Text("Shader Type: %s", StringHelper::ShaderTypeToString(replacement.shaderType).c_str());

			ImGui::Spacing();
			ImGui::Unindent(indentSpace);

			UI_ShaderTargetSourceSection(replacement, HookD3D12::gSelectedShaderTargetIndex);

			ImGui::Spacing();

			if (ImGui::TreeNodeEx("Info##ShaderTargetInfo"))
			{
				ImGui::Text("Profile: %s", replacement.shaderProfile.c_str());
				ImGui::Text("Entry Point: %s", replacement.shaderEntryPoint.c_str());
				ImGui::Text("Hash: %s", replacement.originalShaderBytecodeHash.c_str());
				ImGui::Text("Bytecode Length: %s", replacement.originalShaderBytecodeLength.c_str());
				ImGui::Text("Loaded Modified Blob Bytes: %zu", loadedBlobSize);
				ImGui::Text("Source List: %s", replacement.sourceList.c_str());
				ImGui::Text("Pipeline Index: %s", replacement.pipelineIndex.c_str());
				ImGui::Text("JSON: %s", replacement.jsonPath.c_str());
				ImGui::Text("Original Blob: %s", replacement.originalShaderBlobPath.c_str());
				ImGui::Text("Modified Shader: %s", replacement.modifiedShaderId.c_str());
				ImGui::Separator();
				ImGui::TreePop();
			}

			ImGui::Spacing();

			UI_ShaderTargetPSOList(replacement);

			ImGui::Spacing();

			ImGui::Unindent(indentSpace);
		}
	}

	void UI_ShaderTargetSourceSection(ShaderTarget::ShaderTargetDisk& replacement, int replacementIndex)
	{
		DatabaseModifiedShaders::EnsureModifiedShadersLoaded();

		ImGui::SeparatorText("Modified Shader");
		ImGui::Indent(indentSpace);

		ImGui::Spacing();
		ImGui::Text("Package Folder: %s", ShaderInjectorIO::GetModifiedShadersDirectory().c_str());
		ImGui::Text("Package:");
		ImGui::SameLine();

		const char* btnLabel = "Refresh##ShaderTargetSourceSection";
		float buttonWidth = ImGui::CalcTextSize(btnLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float comboWidth = ImGui::GetContentRegionAvail().x - buttonWidth - spacing;
		ImGui::SetNextItemWidth(comboWidth);

		const ModifiedShader::PackageDisk* selectedPackage = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		const std::string currentPackageName = selectedPackage
			? DatabaseModifiedShaders::DisplayName(*selectedPackage)
			: "(none)";

		if (ImGui::BeginCombo("##ShaderTargetModifiedShader", currentPackageName.c_str()))
		{
			for (const ModifiedShader::PackageDisk& modifiedShader : DatabaseModifiedShaders::GetModifiedShaders())
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

		ImGui::SameLine();
		if (ImGui::Button(btnLabel))
			DatabaseModifiedShaders::RefreshModifiedShaders();

		selectedPackage = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}

	void UI_ShaderTargetPSOList(const ShaderTarget::ShaderTargetDisk& replacement)
	{
		const int replacementCount = CountReplacementPSOs(replacement);
		std::string title = "Replacing Active PSOs: " + std::to_string(replacementCount);

		if (ImGui::TreeNodeEx(title.c_str()))
		{
			if (replacementCount == 0)
			{
				ImGui::Text("No active PSOs are currently using this replacement.");
				return;
			}

			if (ImGui::BeginChild("ReplacementPSOList##SelectedShaderTarget", ImVec2(0, 130), ImGuiChildFlags_Borders))
			{
				for (int i = 0; i < (int)HookD3D12::gGraphicsPipelines.size(); i++)
				{
					const auto& pipeline = HookD3D12::gGraphicsPipelines[i];

					if (PipelineUsesReplacement(pipeline, replacement))
						UI_DrawReplacementPSORow("Graphics", i, pipeline);
				}

				for (int i = 0; i < (int)HookD3D12::gPipelineStates.size(); i++)
				{
					const auto& pipeline = HookD3D12::gPipelineStates[i];

					if (PipelineUsesReplacement(pipeline, replacement))
						UI_DrawReplacementPSORow("Stream", i, pipeline);
				}
			}

			ImGui::EndChild();
			ImGui::TreePop();
		}
	}

	template<typename PipelineT>
	void UI_DrawReplacementPSORow(const char* sourceList, int index, const PipelineT& pipeline)
	{
		ImGui::Text("%s #%d", sourceList, index);
		ImGui::SameLine();
		ImGui::Text("Original: %p", pipeline.pipelineState);
		ImGui::Text("Replacement: %p", pipeline.psoWithReplacement);
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

	template<typename PipelineT>
	bool PipelineUsesReplacement(const PipelineT& pipeline, const ShaderTarget::ShaderTargetDisk& replacement)
	{
		if (!HookD3D12::IsShaderTargetEffectivelyEnabled(replacement) || !pipeline.psoWithReplacement)
			return false;

		return pipeline.activeShaderTargetType == replacement.shaderType
			&& pipeline.activeShaderTargetHash == Hash::ParseHashText(replacement.originalShaderBytecodeHash);
	}
}
