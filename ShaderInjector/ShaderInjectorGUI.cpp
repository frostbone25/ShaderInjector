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
	static const float indentSpace = 16.0f;

	std::string runtimeLogText;
	static std::mutex gRuntimeLogMutex;

	static bool injectorDeveloperSettings = false;

	static int gSelectionStyleIndex = (int)HookD3D12::PixelShaderSelectionStyle::BluePixelShader;
	static std::string gSelectedModifiedShaderId;
	static std::string gModifiedShaderNameBufferId;
	static char gModifiedShaderNameBuffer[256]{};
	static char gShaderConfigurationSearch[256]{};
	static bool gShaderConfigurationDirty = false;
	static std::string gSelectedRenderPassId;
	static std::string gRenderPassNameBufferId;
	static char gRenderPassNameBuffer[256]{};

	struct MipSourceBindingOption
	{
		std::string name;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
	};

	std::vector<MipSourceBindingOption> CollectMipSourceBindingOptions(
		const ModifiedShader::PackageDisk* modifiedShader)
	{
		std::vector<MipSourceBindingOption> options;
		if (!modifiedShader)
			return options;

		for (const ModifiedShader::TargetDisk& target : modifiedShader->targets)
		{
			for (const ShaderAnalysis::ResourceBindingDisk& resource : target.shaderAnalysis.resourceBindings)
			{
				if (resource.type != D3D_SIT_TEXTURE ||
					resource.dimension != D3D_SRV_DIMENSION_TEXTURE2D ||
					resource.bindCount == 0)
				{
					continue;
				}

				const uint32_t boundedCount = resource.bindCount == UINT_MAX
					? 1u
					: (std::min)(resource.bindCount, 64u);
				for (uint32_t bindingIndex = 0; bindingIndex < boundedCount; ++bindingIndex)
				{
					MipSourceBindingOption option{};
					option.name = resource.name.empty() ? "Texture2D" : resource.name;
					if (boundedCount > 1)
						option.name += "[" + std::to_string(bindingIndex) + "]";
					option.shaderRegister = resource.bindPoint + bindingIndex;
					option.registerSpace = resource.registerSpace;

					const bool duplicate = std::any_of(options.begin(), options.end(), [&](const auto& existing)
					{
						return existing.shaderRegister == option.shaderRegister &&
							existing.registerSpace == option.registerSpace;
					});
					if (!duplicate)
						options.push_back(std::move(option));
				}
			}
		}

		return options;
	}

	void ApplyDefaultMipSourceBinding(
		RenderPass::RenderPassDisk& renderPass,
		const ModifiedShader::PackageDisk* modifiedShader)
	{
		const std::vector<MipSourceBindingOption> options =
			CollectMipSourceBindingOptions(modifiedShader);
		if (options.empty())
			return;

		renderPass.sourceTextureShaderRegister = options.front().shaderRegister;
		renderPass.sourceTextureRegisterSpace = options.front().registerSpace;
	}

	const char* ResourceDimensionName(uint32_t dimension)
	{
		switch (static_cast<D3D12_RESOURCE_DIMENSION>(dimension))
		{
			case D3D12_RESOURCE_DIMENSION_BUFFER: return "Buffer";
			case D3D12_RESOURCE_DIMENSION_TEXTURE1D: return "Texture1D";
			case D3D12_RESOURCE_DIMENSION_TEXTURE2D: return "Texture2D";
			case D3D12_RESOURCE_DIMENSION_TEXTURE3D: return "Texture3D";
			default: return "Unknown";
		}
	}

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

	struct ModifiedShaderBatchRecompileResult
	{
		int selectedPackageCount = 0;
		int skippedInactivePackageCount = 0;
		int compiledPackageCount = 0;
		int failedPackageCount = 0;
		int linkedShaderTargetCount = 0;
		int reloadedShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};

	ModifiedShaderBatchRecompileResult RecompileModifiedShaders(bool includeInactivePackages)
	{
		ModifiedShaderBatchRecompileResult batchResult{};
		const std::vector<ModifiedShader::PackageDisk>& modifiedShaders =
			DatabaseModifiedShaders::GetModifiedShaders();
		std::vector<std::string> selectedModifiedShaderIds;
		selectedModifiedShaderIds.reserve(modifiedShaders.size());

		for (const ModifiedShader::PackageDisk& modifiedShader : modifiedShaders)
		{
			const bool active =
				modifiedShader.enabled &&
				ModifiedShaderIsUsedByEnabledShaderTarget(modifiedShader.id);

			if (!includeInactivePackages && !active)
			{
				++batchResult.skippedInactivePackageCount;
				continue;
			}

			selectedModifiedShaderIds.push_back(modifiedShader.id);
		}

		batchResult.selectedPackageCount =
			static_cast<int>(selectedModifiedShaderIds.size());

		for (const std::string& modifiedShaderId : selectedModifiedShaderIds)
		{
			const ModifiedShaderRecompileResult result =
				RecompileModifiedShaderAndReloadLinkedTargets(modifiedShaderId);

			if (!result.compiled)
			{
				++batchResult.failedPackageCount;
				WriteToRuntimeLogError(
					"Failed to compile Modified Shader: " + modifiedShaderId);
				continue;
			}

			++batchResult.compiledPackageCount;
			batchResult.linkedShaderTargetCount += result.linkedShaderTargetCount;
			batchResult.reloadedShaderTargetCount += result.reloadedShaderTargetCount;
			batchResult.skippedInactiveShaderTargetCount +=
				result.skippedInactiveShaderTargetCount;
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

			if (ImGui::IsItemDeactivatedAfterEdit() &&
				!ShaderInjectorIO::WriteInjectorMenuScale(Globals::gShaderInjectorGUIScale))
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

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MODIFIED SHADERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MODIFIED SHADERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MODIFIED SHADERS |||||||||||||||||||||||||||||||||||||||||||||||||||||

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
			ImGui::SetNextItemWidth((std::max)(
				80.0f,
				ImGui::GetContentRegionAvail().x - saveNameButtonWidth - ImGui::GetStyle().ItemSpacing.x));
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

	bool ShaderConfigurationPropertyMatchesSearch(
		const ShaderConfiguration::PropertyDisk& property,
		const std::string& lowercaseSearch)
	{
		if (lowercaseSearch.empty())
			return true;

		const std::string searchableText = StringHelper::LowercaseAscii(
			property.name + "\n" +
			property.sourceFile + "\n" +
			property.sourcePath + "\n" +
			property.comment);
		return searchableText.find(lowercaseSearch) != std::string::npos;
	}

	std::string ShaderConfigurationSourceDisplayName(
		const ShaderConfiguration::PropertyDisk& property)
	{
		std::string displayName = property.sourceFile;
		if (StringHelper::EndsWithIgnoreCase(
			displayName,
			ShaderInjectorIO::extensionHLSL))
		{
			displayName.resize(
				displayName.size() -
				ShaderInjectorIO::extensionHLSL.size());
		}

		return displayName;
	}

	bool UI_ShaderConfigurationProperty(ShaderConfiguration::PropertyDisk& property)
	{
		bool changed = false;
		ImGui::PushID(property.id.c_str());

		ImGui::TextUnformatted(property.name.c_str());
		ImGui::SameLine();
		ImGui::SmallButton("?");

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
			if (property.comment.empty())
				ImGui::TextUnformatted("No description was provided.");
			else
				ImGui::TextUnformatted(property.comment.c_str());

			ImGui::Separator();
			ImGui::Text("Type: %s", property.type.c_str());
			ImGui::Text("Default: %s", property.defaultValue.c_str());
			if (!property.range.empty())
				ImGui::Text("Range: %s", property.range.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		ImGui::TextDisabled("Shader: %s", property.sourceFile.c_str());
		if (ImGui::IsItemHovered() && property.sourcePath != property.sourceFile)
			ImGui::SetTooltip("%s", property.sourcePath.c_str());

		ImGui::SetNextItemWidth(-FLT_MIN);

		if (property.type == "bool")
		{
			bool value = false;
			if (!ShaderConfiguration::TryGetBoolean(property, value))
			{
				ShaderConfiguration::PropertyDisk defaultProperty = property;
				defaultProperty.value = property.defaultValue;
				ShaderConfiguration::TryGetBoolean(defaultProperty, value);
			}

			if (ImGui::Checkbox("##Value", &value))
			{
				ShaderConfiguration::SetBoolean(property, value);
				changed = true;
			}
		}
		else if (property.type == "int")
		{
			int value = 0;
			if (!ShaderConfiguration::TryGetInteger(property, value))
			{
				ShaderConfiguration::PropertyDisk defaultProperty = property;
				defaultProperty.value = property.defaultValue;
				ShaderConfiguration::TryGetInteger(defaultProperty, value);
			}

			float minimum = 0.0f;
			float maximum = 0.0f;
			const bool hasRange =
				ShaderConfiguration::TryGetRange(property, minimum, maximum);
			const bool valueChanged = hasRange
				? ImGui::SliderInt(
					"##Value",
					&value,
					static_cast<int>(minimum),
					static_cast<int>(maximum))
				: ImGui::DragInt("##Value", &value, 1.0f);

			if (valueChanged)
			{
				ShaderConfiguration::SetInteger(property, value);
				changed = true;
			}
		}
		else
		{
			const size_t componentCount =
				ShaderConfiguration::ComponentCount(property);
			std::vector<float> parsedValues;

			if (!ShaderConfiguration::TryGetFloatComponents(property, parsedValues))
			{
				ShaderConfiguration::PropertyDisk defaultProperty = property;
				defaultProperty.value = property.defaultValue;
				ShaderConfiguration::TryGetFloatComponents(
					defaultProperty,
					parsedValues);
			}

			float values[4]{};
			for (size_t index = 0;
				index < componentCount && index < parsedValues.size();
				++index)
			{
				values[index] = parsedValues[index];
			}

			float minimum = 0.0f;
			float maximum = 0.0f;
			const bool hasRange =
				ShaderConfiguration::TryGetRange(property, minimum, maximum);
			const float dragSpeed = hasRange
				? (std::max)(0.001f, (maximum - minimum) / 200.0f)
				: 0.01f;
			bool valueChanged = false;

			switch (componentCount)
			{
			case 1:
				valueChanged = hasRange
					? ImGui::SliderFloat("##Value", values, minimum, maximum)
					: ImGui::DragFloat("##Value", values, dragSpeed);
				break;
			case 2:
				valueChanged = hasRange
					? ImGui::SliderFloat2("##Value", values, minimum, maximum)
					: ImGui::DragFloat2("##Value", values, dragSpeed);
				break;
			case 3:
				valueChanged = hasRange
					? ImGui::SliderFloat3("##Value", values, minimum, maximum)
					: ImGui::DragFloat3("##Value", values, dragSpeed);
				break;
			case 4:
				valueChanged = hasRange
					? ImGui::SliderFloat4("##Value", values, minimum, maximum)
					: ImGui::DragFloat4("##Value", values, dragSpeed);
				break;
			default:
				ImGui::TextDisabled("Unsupported configuration type: %s", property.type.c_str());
				break;
			}

			if (valueChanged)
			{
				ShaderConfiguration::SetFloatComponents(
					property,
					values,
					componentCount);
				changed = true;
			}
		}

		ImGui::Separator();
		ImGui::PopID();
		return changed;
	}

	void UI_ShaderConfiguration()
	{
		if (!ImGui::CollapsingHeader("Shader Configuration"))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		DatabaseShaderConfigurations::EnsureLoaded();
		ShaderConfiguration::DocumentDisk& configurationDocument =
			DatabaseShaderConfigurations::GetEditableDocument();

		ImGui::Text("Properties: %zu", configurationDocument.properties.size());
		if (gShaderConfigurationDirty)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(unsaved changes)");
		}

		ImGui::BeginDisabled(configurationDocument.properties.empty());
		if (ImGui::Button("Apply Changes##ShaderConfiguration"))
		{
			const DatabaseShaderConfigurations::ApplyResult applyResult =
				DatabaseShaderConfigurations::ApplyChanges();

			if (!applyResult.succeeded)
			{
				WriteToRuntimeLogError(
					"Failed to apply Shader Configuration: " +
					applyResult.errorMessage);
			}
			else
			{
				gShaderConfigurationDirty = false;
				WriteToRuntimeLogSuccess(
					"Applied Shader Configuration properties=" +
					std::to_string(applyResult.propertyCount) +
					" sourceFiles=" +
					std::to_string(applyResult.sourceFileCount));
				RecompileModifiedShaders(false);
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Reload Properties##ShaderConfiguration"))
		{
			if (DatabaseShaderConfigurations::ReloadProperties())
			{
				gShaderConfigurationDirty = false;
				WriteToRuntimeLogSuccess(
					"Reloaded Shader Configuration properties.");
			}
			else
			{
				WriteToRuntimeLogError(
					"Failed to reload Shader Configuration properties.");
			}
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint(
			"##ShaderConfigurationSearch",
			"Search properties...",
			gShaderConfigurationSearch,
			sizeof(gShaderConfigurationSearch));

		const std::string lowercaseSearch = StringHelper::LowercaseAscii(
			StringHelper::TrimWhitespace(gShaderConfigurationSearch));
		size_t visiblePropertyCount = 0;

		if (ImGui::BeginChild(
			"ShaderConfiguration##PropertyList",
			ImVec2(0, 320),
			ImGuiChildFlags_Borders))
		{
			size_t sourceGroupStart = 0;
			while (sourceGroupStart < configurationDocument.properties.size())
			{
				ShaderConfiguration::PropertyDisk& firstProperty =
					configurationDocument.properties[sourceGroupStart];
				size_t sourceGroupEnd = sourceGroupStart + 1;

				while (sourceGroupEnd <
					configurationDocument.properties.size() &&
					configurationDocument.properties[sourceGroupEnd].sourcePath ==
						firstProperty.sourcePath)
				{
					++sourceGroupEnd;
				}

				size_t matchingPropertyCount = 0;
				for (size_t propertyIndex = sourceGroupStart;
					propertyIndex < sourceGroupEnd;
					++propertyIndex)
				{
					if (ShaderConfigurationPropertyMatchesSearch(
						configurationDocument.properties[propertyIndex],
						lowercaseSearch))
					{
						++matchingPropertyCount;
					}
				}

				if (matchingPropertyCount > 0)
				{
					const size_t totalPropertyCount =
						sourceGroupEnd - sourceGroupStart;
					std::string groupLabel =
						ShaderConfigurationSourceDisplayName(firstProperty) +
						": " + std::to_string(totalPropertyCount);

					if (!lowercaseSearch.empty() &&
						matchingPropertyCount != totalPropertyCount)
					{
						groupLabel +=
							" (" + std::to_string(matchingPropertyCount) +
							" matching)";
					}

					ImGui::PushID(firstProperty.sourcePath.c_str());
					if (!lowercaseSearch.empty())
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);

					const bool sourceGroupOpen = ImGui::TreeNodeEx(
						"##ShaderConfigurationSource",
						ImGuiTreeNodeFlags_SpanAvailWidth,
						"%s",
						groupLabel.c_str());

					if (ImGui::IsItemHovered() &&
						firstProperty.sourcePath != firstProperty.sourceFile)
					{
						ImGui::SetTooltip(
							"%s",
							firstProperty.sourcePath.c_str());
					}

					if (sourceGroupOpen)
					{
						for (size_t propertyIndex = sourceGroupStart;
							propertyIndex < sourceGroupEnd;
							++propertyIndex)
						{
							ShaderConfiguration::PropertyDisk& property =
								configurationDocument.properties[propertyIndex];
							if (!ShaderConfigurationPropertyMatchesSearch(
								property,
								lowercaseSearch))
							{
								continue;
							}

							++visiblePropertyCount;
							if (UI_ShaderConfigurationProperty(property))
								gShaderConfigurationDirty = true;
						}

						ImGui::TreePop();
					}
					else
					{
						visiblePropertyCount += matchingPropertyCount;
					}

					ImGui::PopID();
				}

				sourceGroupStart = sourceGroupEnd;
			}

			if (configurationDocument.properties.empty())
			{
				ImGui::TextWrapped(
					"No configurable #define properties were found in ModifiedShaders.");
			}
			else if (visiblePropertyCount == 0)
			{
				ImGui::TextUnformatted("No properties match the current search.");
			}
		}
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||

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

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RENDER PASSES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RENDER PASSES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RENDER PASSES |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_RenderPasses()
	{
		if (!ImGui::CollapsingHeader("Render Passes"))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();
		DatabaseRenderPasses::EnsureRenderPassesLoaded();
		DatabaseModifiedShaders::EnsureModifiedShadersLoaded();

		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();

		const std::vector<RenderPass::RenderPassDisk>& renderPasses = DatabaseRenderPasses::GetRenderPasses();
		ImGui::Text("Loaded: %zu", renderPasses.size());
		ImGui::SameLine();

		if (ImGui::Button("Refresh##RenderPasses"))
		{
			DatabaseModifiedShaders::RefreshModifiedShaders();
			HookD3D12::RefreshLoadedShaderTargets();
			DatabaseRenderPasses::RefreshRenderPasses();
			if (!gSelectedRenderPassId.empty() && !DatabaseRenderPasses::FindRenderPassById(gSelectedRenderPassId))
				gSelectedRenderPassId.clear();
		}

		ImGui::SameLine();
		if (ImGui::Button("Open Folder##RenderPasses") &&
			!ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetRenderPassesDirectory()))
		{
			WriteToRuntimeLogError("Could not open the Render Passes folder.");
		}

		ImGui::SameLine();
		if (ImGui::Button("Create Render Pass"))
		{
			std::string newRenderPassId;
			if (DatabaseRenderPasses::CreateRenderPass(newRenderPassId))
			{
				gSelectedRenderPassId = newRenderPassId;
				gRenderPassNameBufferId.clear();
				WriteToRuntimeLogSuccess("Created Render Pass: " + newRenderPassId);
			}
			else
			{
				WriteToRuntimeLogError("Failed to create Render Pass.");
			}
		}

		const std::vector<RenderPass::RenderPassDisk>& refreshedRenderPasses = DatabaseRenderPasses::GetRenderPasses();
		if (refreshedRenderPasses.empty())
		{
			ImGui::TextUnformatted("No Render Passes found.");
			ImGui::Spacing();
			ImGui::Unindent(indentSpace);
			return;
		}

		if (gSelectedRenderPassId.empty() || !DatabaseRenderPasses::FindRenderPassById(gSelectedRenderPassId))
			gSelectedRenderPassId = refreshedRenderPasses.front().id;

		if (ImGui::BeginChild("RenderPassList##RenderPasses", ImVec2(0, 180), ImGuiChildFlags_Borders))
		{
			for (const RenderPass::RenderPassDisk& renderPass : refreshedRenderPasses)
			{
				std::string label = renderPass.name.empty() ? renderPass.id : renderPass.name;
				const ModifiedShader::PackageDisk* modifiedShader =
					DatabaseRenderPasses::ResolveModifiedShader(renderPass);
				const bool eventChainActive = DatabaseRenderPasses::IsEventChainActive(renderPass);
				bool hasResolvedShaderTarget = false;
				if (modifiedShader)
				{
					for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
					{
						if (shaderTarget.modifiedShaderId == modifiedShader->id &&
							HookD3D12::IsShaderTargetEffectivelyEnabled(shaderTarget))
						{
							hasResolvedShaderTarget = true;
							break;
						}
					}
				}
				if (!renderPass.enabled)
					label += " (disabled)";
				else if (renderPass.event.id.empty())
					label += " (event not set)";
				else if (!eventChainActive)
					label += " (event unavailable)";
				else if (!modifiedShader)
					label += " (shader missing)";
				else if (!modifiedShader->enabled)
					label += " (shader disabled)";
				else if (!hasResolvedShaderTarget)
					label += " (waiting for target)";
				label += " [" + std::string(RenderPass::TypeName(renderPass.type)) + "]";
				label += "##RenderPass_" + renderPass.id;

				const bool selected = renderPass.id == gSelectedRenderPassId;
				const bool active = renderPass.enabled && eventChainActive && modifiedShader &&
					modifiedShader->enabled && hasResolvedShaderTarget;
				if (active)
				{
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
				}

				if (ImGui::Selectable(label.c_str(), selected || active))
				{
					gSelectedRenderPassId = renderPass.id;
					gRenderPassNameBufferId.clear();
				}

				if (active)
					ImGui::PopStyleColor(3);
			}
		}
		ImGui::EndChild();

		RenderPass::RenderPassDisk* renderPass = DatabaseRenderPasses::FindRenderPassById(gSelectedRenderPassId);
		if (!renderPass)
		{
			ImGui::Unindent(indentSpace);
			return;
		}

		if (gRenderPassNameBufferId != renderPass->id)
		{
			strncpy_s(gRenderPassNameBuffer, renderPass->name.c_str(), _TRUNCATE);
			gRenderPassNameBufferId = renderPass->id;
		}

		ImGui::SeparatorText(renderPass->name.empty() ? renderPass->id.c_str() : renderPass->name.c_str());
		ImGui::Indent(indentSpace);

		ImGui::TextUnformatted("Name");
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText("##RenderPassName", gRenderPassNameBuffer, sizeof(gRenderPassNameBuffer));
		ImGui::TextUnformatted("Enabled");
		ImGui::Checkbox("##RenderPassEnabled", &renderPass->enabled);

		ImGui::TextUnformatted("Type");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##RenderPassType", RenderPass::TypeName(renderPass->type)))
		{
			const RenderPass::RenderPassType typeOptions[] = {
				RenderPass::RenderPassType::Custom,
				RenderPass::RenderPassType::MipChain,
			};
			for (RenderPass::RenderPassType typeOption : typeOptions)
			{
				const bool selected = renderPass->type == typeOption;
				if (ImGui::Selectable(RenderPass::TypeName(typeOption), selected) && !selected)
				{
					renderPass->type = typeOption;
					renderPass->timing = RenderPass::timingBefore;
					renderPass->vertexShaderSourceFile.clear();
					renderPass->fragmentShaderSourceFile.clear();
					renderPass->vertexShaderCompiledBlobFile.clear();
					renderPass->fragmentShaderCompiledBlobFile.clear();
					renderPass->vertexShaderBlob.clear();
					renderPass->fragmentShaderBlob.clear();
					renderPass->vertexShaderBlobHash = 0;
					renderPass->fragmentShaderBlobHash = 0;
					RenderPass::ResolveShaderPaths(*renderPass);
					if (typeOption == RenderPass::RenderPassType::MipChain)
					{
						ApplyDefaultMipSourceBinding(
							*renderPass,
							DatabaseRenderPasses::ResolveModifiedShader(*renderPass));
					}
				}
			}
			ImGui::EndCombo();
		}

		ImGui::TextUnformatted("Track Resource Bindings");
		ImGui::Checkbox("##RenderPassTrackResourceBindings", &renderPass->trackResourceBindings);
		if (renderPass->trackResourceBindings)
		{
			int maximumTrackedDescriptors = static_cast<int>(renderPass->maximumTrackedDescriptors);
			ImGui::TextUnformatted("Maximum Table Descriptors");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragInt(
				"##RenderPassMaximumTableDescriptors",
				&maximumTrackedDescriptors,
				1.0f,
				1,
				1024))
			{
				renderPass->maximumTrackedDescriptors = static_cast<uint32_t>((std::max)(1, maximumTrackedDescriptors));
			}
		}

		ImGui::TextUnformatted("Event Type");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##RenderPassEventType", RenderPass::EventTypeName(renderPass->event.type)))
		{
			const RenderPass::EventType eventTypes[] = {
				RenderPass::EventType::ModifiedShader,
				RenderPass::EventType::RenderPass,
			};
			for (RenderPass::EventType eventType : eventTypes)
			{
				const bool selected = renderPass->event.type == eventType;
				if (ImGui::Selectable(RenderPass::EventTypeName(eventType), selected) && !selected)
				{
					renderPass->event.type = eventType;
					renderPass->event.id.clear();
				}
			}
			ImGui::EndCombo();
		}

		std::string eventPreview = "(none)";
		if (!renderPass->event.id.empty())
		{
			if (renderPass->event.type == RenderPass::EventType::ModifiedShader)
			{
				const ModifiedShader::PackageDisk* eventModifiedShader =
					DatabaseModifiedShaders::FindModifiedShaderById(renderPass->event.id);
				eventPreview = eventModifiedShader
					? DatabaseModifiedShaders::DisplayName(*eventModifiedShader)
					: renderPass->event.id + " (missing)";
			}
			else
			{
				const RenderPass::RenderPassDisk* eventRenderPass =
					DatabaseRenderPasses::FindRenderPassByIdReadOnly(renderPass->event.id);
				eventPreview = eventRenderPass
					? eventRenderPass->name
					: renderPass->event.id + " (missing)";
			}
		}

		ImGui::TextUnformatted("Event");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##RenderPassEvent", eventPreview.c_str()))
		{
			if (ImGui::Selectable("(none)", renderPass->event.id.empty()))
				renderPass->event.id.clear();

			if (renderPass->event.type == RenderPass::EventType::ModifiedShader)
			{
				const std::vector<ModifiedShader::PackageDisk>& modifiedShaders =
					DatabaseModifiedShaders::GetModifiedShaders();
				for (size_t modifiedShaderIndex = 0; modifiedShaderIndex < modifiedShaders.size(); ++modifiedShaderIndex)
				{
					const ModifiedShader::PackageDisk& modifiedShader = modifiedShaders[modifiedShaderIndex];
					const bool selected = modifiedShader.id == renderPass->event.id;
					std::string label = DatabaseModifiedShaders::DisplayName(modifiedShader);
					if (!modifiedShader.enabled)
						label += " (disabled)";
					label += "##RenderPassEventModifiedShader_" + std::to_string(modifiedShaderIndex);
					if (ImGui::Selectable(label.c_str(), selected))
					{
						renderPass->event.id = modifiedShader.id;
						if (renderPass->type == RenderPass::RenderPassType::MipChain)
							ApplyDefaultMipSourceBinding(*renderPass, &modifiedShader);
					}
				}
			}
			else
			{
				for (size_t eventRenderPassIndex = 0;
					eventRenderPassIndex < refreshedRenderPasses.size();
					++eventRenderPassIndex)
				{
					const RenderPass::RenderPassDisk& eventRenderPass =
						refreshedRenderPasses[eventRenderPassIndex];
					if (!DatabaseRenderPasses::CanReferenceRenderPass(renderPass->id, eventRenderPass.id))
						continue;

					const bool selected = eventRenderPass.id == renderPass->event.id;
					std::string label = eventRenderPass.name + " [" +
						RenderPass::TypeName(eventRenderPass.type) + "]";
					if (!eventRenderPass.enabled)
						label += " (disabled)";
					label += "##RenderPassEventRenderPass_" + std::to_string(eventRenderPassIndex);
					if (ImGui::Selectable(label.c_str(), selected))
					{
						renderPass->event.id = eventRenderPass.id;
						if (renderPass->type == RenderPass::RenderPassType::MipChain)
						{
							ApplyDefaultMipSourceBinding(
								*renderPass,
								DatabaseRenderPasses::ResolveModifiedShader(eventRenderPass));
						}
					}
				}
			}
			ImGui::EndCombo();
		}

		const ModifiedShader::PackageDisk* selectedModifiedShader =
			DatabaseRenderPasses::ResolveModifiedShader(*renderPass);
		const bool mipChainPass = renderPass->type == RenderPass::RenderPassType::MipChain;
		const bool directMipChainEvent = mipChainPass &&
			renderPass->event.type == RenderPass::EventType::ModifiedShader;
		if (directMipChainEvent)
			renderPass->timing = RenderPass::timingBefore;
		ImGui::TextUnformatted("Timing");
		ImGui::BeginDisabled(directMipChainEvent);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##RenderPassTiming", renderPass->timing.c_str()))
		{
			const char* timingOptions[] = { RenderPass::timingBefore, RenderPass::timingAfter };
			for (const char* timingOption : timingOptions)
			{
				const bool selected = renderPass->timing == timingOption;
				if (ImGui::Selectable(timingOption, selected))
					renderPass->timing = timingOption;
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		const std::string resolvedRootTiming = DatabaseRenderPasses::ResolveRootTiming(*renderPass);
		if (mipChainPass && resolvedRootTiming == RenderPass::timingAfter)
		{
			ImGui::TextWrapped(
				"A MipChain event must resolve to a graph that executes before the Modified Shader draw.");
		}
		else if (!renderPass->event.id.empty() && !selectedModifiedShader)
		{
			ImGui::TextWrapped(
				"The selected event chain does not currently resolve to an available Modified Shader.");
		}

		if (mipChainPass)
		{
			const std::vector<MipSourceBindingOption> sourceOptions =
				CollectMipSourceBindingOptions(selectedModifiedShader);
			const auto selectedSourceIt = std::find_if(sourceOptions.begin(), sourceOptions.end(), [&](const auto& option)
			{
				return option.shaderRegister == renderPass->sourceTextureShaderRegister &&
					option.registerSpace == renderPass->sourceTextureRegisterSpace;
			});
			const std::string sourcePreview = selectedSourceIt != sourceOptions.end()
				? selectedSourceIt->name + " (t" + std::to_string(selectedSourceIt->shaderRegister) +
					", space" + std::to_string(selectedSourceIt->registerSpace) + ")"
				: "Manual binding (t" + std::to_string(renderPass->sourceTextureShaderRegister) +
					", space" + std::to_string(renderPass->sourceTextureRegisterSpace) + ")";

			ImGui::TextUnformatted("Source Texture");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##RenderPassMipSourceTexture", sourcePreview.c_str()))
			{
				for (size_t optionIndex = 0; optionIndex < sourceOptions.size(); ++optionIndex)
				{
					const MipSourceBindingOption& option = sourceOptions[optionIndex];
					const bool selected = option.shaderRegister == renderPass->sourceTextureShaderRegister &&
						option.registerSpace == renderPass->sourceTextureRegisterSpace;
					const std::string optionLabel = option.name + " (t" +
						std::to_string(option.shaderRegister) + ", space" +
						std::to_string(option.registerSpace) + ")##MipSource_" +
						std::to_string(optionIndex);
					if (ImGui::Selectable(optionLabel.c_str(), selected))
					{
						renderPass->sourceTextureShaderRegister = option.shaderRegister;
						renderPass->sourceTextureRegisterSpace = option.registerSpace;
					}
				}
				ImGui::EndCombo();
			}
			if (sourceOptions.empty())
				ImGui::TextWrapped("No reflected Texture2D bindings are available for the selected Modified Shader.");
			else if (selectedSourceIt == sourceOptions.end())
			{
				const auto sameRegisterIt = std::find_if(sourceOptions.begin(), sourceOptions.end(), [&](const auto& option)
				{
					return option.shaderRegister == renderPass->sourceTextureShaderRegister;
				});
				if (sameRegisterIt != sourceOptions.end())
				{
					ImGui::TextWrapped(
						"The configured register space does not match the reflected %s binding (t%u, space%u). Select it above and save the Render Pass.",
						sameRegisterIt->name.c_str(),
						sameRegisterIt->shaderRegister,
						sameRegisterIt->registerSpace);
				}
				else
				{
					ImGui::TextWrapped(
						"The manual binding is not present in the selected Modified Shader's reflected Texture2D resources.");
				}
			}

			int shaderRegister = static_cast<int>(renderPass->sourceTextureShaderRegister);
			int registerSpace = static_cast<int>(renderPass->sourceTextureRegisterSpace);
			ImGui::TextUnformatted("Source Texture Register (t)");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragInt("##RenderPassSourceTextureRegister", &shaderRegister, 1.0f, 0, 4095))
				renderPass->sourceTextureShaderRegister = static_cast<uint32_t>((std::max)(0, shaderRegister));
			ImGui::TextUnformatted("Register Space");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragInt("##RenderPassSourceTextureRegisterSpace", &registerSpace, 1.0f, 0, 4095))
				renderPass->sourceTextureRegisterSpace = static_cast<uint32_t>((std::max)(0, registerSpace));
		}

		ImGui::SeparatorText(mipChainPass ? "Mip Chain Shader" : "Fullscreen Fragment Shader");
		const bool hasShaderTemplate = RenderPass::HasShaderTemplate(*renderPass);
		const bool hasCompiledShaders = RenderPass::HasCompiledShaders(*renderPass);
		ImGui::Text("Source: %s", hasShaderTemplate ? "Ready" : "Not created");
		ImGui::Text("Compiled: %s", hasCompiledShaders ? "Ready" : "Not loaded");
		const bool canCreateShaderTemplate = selectedModifiedShader &&
			selectedModifiedShader->shaderType == ShaderTarget::PixelShader;
		if (!hasShaderTemplate)
		{
			ImGui::BeginDisabled(!canCreateShaderTemplate);
			if (ImGui::Button(mipChainPass
				? "Create Mip Chain Shader Template"
				: "Create Render Pass Fragment Shader Template"))
			{
				std::string error;
				if (DatabaseRenderPasses::CreateFragmentShaderTemplate(renderPass->id, error))
					WriteToRuntimeLogSuccess("Created and compiled Render Pass shader template: " + renderPass->name);
				else
					WriteToRuntimeLogError("Could not create Render Pass shader template: " + error);
			}
			ImGui::EndDisabled();
		}
		else
		{
			if (ImGui::Button("Recompile Render Pass Shaders"))
			{
				std::string error;
				if (DatabaseRenderPasses::CompileRenderPassShaders(renderPass->id, error))
					WriteToRuntimeLogSuccess("Recompiled Render Pass shaders: " + renderPass->name);
				else
					WriteToRuntimeLogError("Could not compile Render Pass shaders: " + error);
			}
			ImGui::SameLine();
			if (ImGui::Button("Open Fragment Shader") &&
				!ShaderInjectorIO::OpenFile(renderPass->fragmentShaderSourcePath))
			{
				WriteToRuntimeLogError("Could not open Render Pass fragment shader: " + renderPass->fragmentShaderSourcePath);
			}
		}
		if (!canCreateShaderTemplate && !hasShaderTemplate)
			ImGui::TextUnformatted("Select an event that resolves to a pixel Modified Shader to create this template.");

		if (ImGui::Button("Save##RenderPass"))
		{
			renderPass->name = StringHelper::TrimWhitespace(gRenderPassNameBuffer);
			if (renderPass->name.empty())
			{
				WriteToRuntimeLogError("Render Pass name cannot be empty.");
			}
			else if (DatabaseRenderPasses::SaveRenderPass(renderPass->id))
			{
				WriteToRuntimeLogSuccess("Saved Render Pass: " + renderPass->name);
			}
			else
			{
				WriteToRuntimeLogError("Failed to save Render Pass: " + renderPass->id);
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Delete##RenderPass"))
		{
			const std::string deletedName = renderPass->name;
			if (DatabaseRenderPasses::DeleteRenderPass(renderPass->id))
			{
				gSelectedRenderPassId.clear();
				gRenderPassNameBufferId.clear();
				WriteToRuntimeLogSuccess("Deleted Render Pass: " + deletedName);
			}
			else
			{
				WriteToRuntimeLogError("Failed to delete Render Pass: " + deletedName);
			}
			ImGui::Unindent(indentSpace);
			ImGui::Unindent(indentSpace);
			return;
		}

		ImGui::SameLine();
		if (ImGui::Button("Open Folder##SelectedRenderPass") &&
			!ShaderInjectorIO::OpenDirectory(renderPass->packageDirectory))
		{
			WriteToRuntimeLogError("Could not open Render Pass package folder: " + renderPass->packageDirectory);
		}

		if (ImGui::TreeNodeEx("Info##RenderPassInfo"))
		{
			const std::string resolvedModifiedShaderId =
				DatabaseRenderPasses::ResolveModifiedShaderId(*renderPass);
			ImGui::Text("ID: %s", renderPass->id.c_str());
			ImGui::Text("Type: %s", RenderPass::TypeName(renderPass->type));
			ImGui::Text("Package: %s", renderPass->packageDirectory.c_str());
			ImGui::Text("JSON: %s", renderPass->jsonPath.c_str());
			ImGui::Text("Event Type: %s", RenderPass::EventTypeName(renderPass->event.type));
			ImGui::Text("Event ID: %s", renderPass->event.id.empty() ? "(none)" : renderPass->event.id.c_str());
			ImGui::Text("Resolved Modified Shader ID: %s", resolvedModifiedShaderId.empty() ? "(none)" : resolvedModifiedShaderId.c_str());
			ImGui::Text("Resolved Root Boundary: %s", resolvedRootTiming.empty() ? "(none)" : resolvedRootTiming.c_str());
			ImGui::Text("Maximum Table Descriptors: %u", renderPass->maximumTrackedDescriptors);
			if (mipChainPass)
			{
				ImGui::Text(
					"Mip Source: t%u, space%u",
					renderPass->sourceTextureShaderRegister,
					renderPass->sourceTextureRegisterSpace);
			}
			ImGui::Text("Vertex Source: %s", renderPass->vertexShaderSourcePath.empty() ? "(none)" : renderPass->vertexShaderSourcePath.c_str());
			ImGui::Text("Fragment Source: %s", renderPass->fragmentShaderSourcePath.empty() ? "(none)" : renderPass->fragmentShaderSourcePath.c_str());
			ImGui::Text("Vertex Blob: %s", renderPass->vertexShaderCompiledBlobPath.empty() ? "(none)" : renderPass->vertexShaderCompiledBlobPath.c_str());
			ImGui::Text("Fragment Blob: %s", renderPass->fragmentShaderCompiledBlobPath.empty() ? "(none)" : renderPass->fragmentShaderCompiledBlobPath.c_str());
			if (selectedModifiedShader)
				ImGui::Text("Shader Type: %s", StringHelper::ShaderTypeToString(selectedModifiedShader->shaderType).c_str());

			int linkedShaderTargetCount = 0;
			if (!resolvedModifiedShaderId.empty())
			{
				for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
				{
					if (shaderTarget.modifiedShaderId == resolvedModifiedShaderId)
						++linkedShaderTargetCount;
				}
			}
			ImGui::Text("Resolved Shader Targets: %d", linkedShaderTargetCount);
			if (!resolvedModifiedShaderId.empty())
			{
				for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
				{
					if (shaderTarget.modifiedShaderId != resolvedModifiedShaderId)
						continue;
					ImGui::BulletText("%s [%s]", shaderTarget.name.c_str(), shaderTarget.originalShaderBytecodeHash.c_str());
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Runtime Diagnostics##RenderPassDiagnostics"))
		{
			const RenderPass::RuntimeDiagnostics diagnostics = RenderPassRuntime::GetDiagnostics(renderPass->id);
			ImGui::Text("Triggers: %llu", static_cast<unsigned long long>(diagnostics.triggerCount));
			ImGui::Text("Executions: %llu", static_cast<unsigned long long>(diagnostics.executionCount));
			ImGui::Text("Execution Failures: %llu", static_cast<unsigned long long>(diagnostics.executionFailureCount));
			ImGui::Text("Last Event: %s:%s",
				diagnostics.lastEventType.empty() ? "(none)" : diagnostics.lastEventType.c_str(),
				diagnostics.lastEventId.empty() ? "(none)" : diagnostics.lastEventId.c_str());
			ImGui::Text("Last Event Timing: %s", diagnostics.lastTiming.empty() ? "(none)" : diagnostics.lastTiming.c_str());
			ImGui::Text("Last Command: %s", diagnostics.lastOperation.empty() ? "(none)" : diagnostics.lastOperation.c_str());
			ImGui::Text("Modified Shader: %s", diagnostics.lastModifiedShaderId.empty() ? "(none)" : diagnostics.lastModifiedShaderId.c_str());
			ImGui::Text("Last Target: %s", diagnostics.lastShaderTargetName.empty() ? "(none)" : diagnostics.lastShaderTargetName.c_str());
			ImGui::Text("Last Hash: %s", diagnostics.lastShaderTargetHash.empty() ? "(none)" : diagnostics.lastShaderTargetHash.c_str());
			if (!diagnostics.lastExecutionError.empty())
				ImGui::TextWrapped("Execution Error: %s", diagnostics.lastExecutionError.c_str());

			if (ImGui::Button("Clear##RenderPassDiagnostics"))
				RenderPassRuntime::ClearDiagnostics(renderPass->id);

			if (ImGui::BeginChild("ResourceBindings##RenderPassDiagnostics", ImVec2(0, 220), ImGuiChildFlags_Borders))
			{
				if (diagnostics.resourceBindings.empty())
				{
					ImGui::TextUnformatted("No resource bindings captured.");
				}
				else
				{
					for (size_t bindingIndex = 0; bindingIndex < diagnostics.resourceBindings.size(); ++bindingIndex)
					{
						const RenderPass::ResourceBindingDiagnostic& binding = diagnostics.resourceBindings[bindingIndex];
						ImGui::PushID(static_cast<int>(bindingIndex));
						if (ImGui::TreeNodeEx("Binding", ImGuiTreeNodeFlags_SpanAvailWidth, "%s: %s", binding.pipeline.c_str(), binding.bindingType.c_str()))
						{
							if (binding.rootParameterIndex != UINT32_MAX)
								ImGui::Text("Root Parameter: %u", binding.rootParameterIndex);
							if (binding.gpuAddress)
								ImGui::Text("GPU Address: 0x%016llX", static_cast<unsigned long long>(binding.gpuAddress));
							if (binding.gpuDescriptorHandle)
								ImGui::Text("GPU Descriptor: 0x%016llX", static_cast<unsigned long long>(binding.gpuDescriptorHandle));
							if (binding.cpuDescriptorHandle)
								ImGui::Text("CPU Descriptor: 0x%016llX", static_cast<unsigned long long>(binding.cpuDescriptorHandle));
							if (binding.descriptorHeapType != UINT32_MAX)
								ImGui::Text("Heap Type: %u", binding.descriptorHeapType);
							if (binding.descriptorIndex != UINT32_MAX)
								ImGui::Text("Descriptor Index: %u", binding.descriptorIndex);
							if (binding.descriptorCount)
								ImGui::Text("Descriptor Count: %u", binding.descriptorCount);
							if (binding.shaderRegister != UINT32_MAX)
							{
								ImGui::Text(
									"Shader Register: %u, Space: %u",
									binding.shaderRegister,
									binding.registerSpace == UINT32_MAX ? 0 : binding.registerSpace);
							}
							if (binding.resourcePointer)
								ImGui::Text("Resource: 0x%016llX", static_cast<unsigned long long>(binding.resourcePointer));
							if (!binding.resourceName.empty())
								ImGui::Text("Resource Name: %s", binding.resourceName.c_str());
							if (binding.resourceDimension != UINT32_MAX)
							{
								ImGui::Text("Dimension: %s", ResourceDimensionName(binding.resourceDimension));
								if (binding.resourceDimension == D3D12_RESOURCE_DIMENSION_BUFFER)
								{
									ImGui::Text("Buffer Offset: %llu", static_cast<unsigned long long>(binding.bufferOffset));
									ImGui::Text("Buffer Size: %llu", static_cast<unsigned long long>(binding.bufferSize));
								}
								else
								{
									ImGui::Text(
										"Extent: %llux%ux%u",
										static_cast<unsigned long long>(binding.resourceWidth),
										binding.resourceHeight,
										binding.resourceDepthOrArraySize);
									ImGui::Text("Mip Levels: %u", binding.resourceMipLevels);
									ImGui::Text("Sample Count: %u", binding.resourceSampleCount);
								}
								ImGui::Text("DXGI Format: %u", binding.resourceFormat);
							}
							if (binding.elementCount)
							{
								ImGui::Text("First Element: %llu", static_cast<unsigned long long>(binding.firstElement));
								ImGui::Text("Element Count: %u", binding.elementCount);
								ImGui::Text("Structure Stride: %u", binding.structureByteStride);
							}
							if (!binding.rootConstants.empty())
							{
								std::string constantText = "Values:";
								for (uint32_t value : binding.rootConstants)
									constantText += StringHelper::Format(" 0x%08X", value);
								ImGui::TextWrapped("%s", constantText.c_str());
								ImGui::Text("Destination Offset: %u", binding.destinationOffset);
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}
			}
			ImGui::EndChild();
			ImGui::TreePop();
		}

		ImGui::Unindent(indentSpace);
		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DEVELOPER SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DEVELOPER SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DEVELOPER SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_RenderDoc()
	{
		const bool renderDocAvailable = RenderDocIntegration::IsAvailable();
		const bool frameCaptureActive = RenderDocIntegration::IsFrameCapturing();
		const bool targetControlConnected = RenderDocIntegration::IsTargetControlConnected();

		ImGui::SeparatorText("RenderDoc");
		ImGui::Text("Status: %s", RenderDocIntegration::GetStatusText().c_str());
		ImGui::Text("API Version: %s", RenderDocIntegration::GetApiVersionText().c_str());
		if (renderDocAvailable)
			ImGui::Text("Library Load: %s", RenderDocIntegration::WasLoadedByInjector() ? "Shader Injector" : "External");
		ImGui::Text("Target Control: %s", targetControlConnected ? "Connected" : "Disconnected");
		ImGui::Text("Frame Capture: %s", frameCaptureActive ? "Active" : "Idle");
		ImGui::Text("Captures: %u", RenderDocIntegration::GetCaptureCount());

		const std::string renderDocLibraryPath = RenderDocIntegration::GetLibraryPath();
		if (!renderDocLibraryPath.empty())
			ImGui::TextWrapped("Library: %s", renderDocLibraryPath.c_str());

		const uint32_t replayUiProcessId = RenderDocIntegration::GetReplayUiProcessId();
		if (replayUiProcessId != 0)
			ImGui::Text("Replay UI Process: %u", replayUiProcessId);

		const std::string latestCapturePath = RenderDocIntegration::GetLatestCapturePath();
		if (!latestCapturePath.empty())
			ImGui::TextWrapped("Latest: %s", latestCapturePath.c_str());

		ImGui::Spacing();
		if (!targetControlConnected)
		{
			if (ImGui::Button(renderDocAvailable ? "Connect RenderDoc UI" : "Attach RenderDoc"))
			{
				const RenderDocIntegration::ReplayUiRequestResult result = RenderDocIntegration::ConnectReplayUi();
				switch (result)
				{
					case RenderDocIntegration::ReplayUiRequestResult::Launched:
						WriteToRuntimeLogSuccess("RenderDoc Replay UI launched and requested target control connection.");
						break;
					case RenderDocIntegration::ReplayUiRequestResult::AlreadyConnected:
						WriteToRuntimeLogSuccess("RenderDoc target control is already connected.");
						break;
					case RenderDocIntegration::ReplayUiRequestResult::Disabled:
						WriteToRuntimeLogWarning("RenderDoc integration is disabled in ShaderInjector.ini.");
						break;
					case RenderDocIntegration::ReplayUiRequestResult::Unavailable:
						WriteToRuntimeLogError("RenderDoc installation could not be found or loaded.");
						break;
					case RenderDocIntegration::ReplayUiRequestResult::LaunchFailed:
					default:
						WriteToRuntimeLogError("RenderDoc Replay UI failed to launch.");
						break;
				}
			}

			ImGui::SameLine();
		}

		ImGui::BeginDisabled(!renderDocAvailable || frameCaptureActive);
		if (ImGui::Button("RenderDoc Frame Capture"))
		{
			const RenderDocIntegration::CaptureRequestResult result =
				RenderDocIntegration::RequestFrameCapture(nullptr, Globals::mainWindow);

			switch (result)
			{
				case RenderDocIntegration::CaptureRequestResult::Queued:
					WriteToRuntimeLogSuccess("RenderDoc frame capture queued for the next Present.");
					break;
				case RenderDocIntegration::CaptureRequestResult::AlreadyCapturing:
					WriteToRuntimeLogWarning("RenderDoc is already capturing a frame.");
					break;
				case RenderDocIntegration::CaptureRequestResult::Disabled:
					WriteToRuntimeLogWarning("RenderDoc integration is disabled in ShaderInjector.ini.");
					break;
				case RenderDocIntegration::CaptureRequestResult::TargetUnavailable:
					WriteToRuntimeLogError("The game D3D12 device or window is not ready for capture.");
					break;
				case RenderDocIntegration::CaptureRequestResult::Unavailable:
				default:
					WriteToRuntimeLogError("RenderDoc is not attached to this process.");
					break;
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Refresh##RenderDoc"))
		{
			RenderDocIntegration::Refresh();
			WriteToRuntimeLog("RenderDoc status refreshed: " + RenderDocIntegration::GetStatusText());
		}
	}

	void UI_DeveloperSettings()
	{
		if (ImGui::CollapsingHeader("Developer Settings"))
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			if (ImGui::BeginTabBar("##DeveloperSettingsTabs"))
			{
				if (ImGui::BeginTabItem("Shader Inspector"))
				{
					ImGui::InputTextMultiline("##DeveloperSettingsNote",
						const_cast<char*>(noteDeveloperSettingsText),
						strlen(noteDeveloperSettingsText) + 1,
						ImVec2(-FLT_MIN, 0), // -FLT_MIN width = stretch to window edge, 0 height = auto
						ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap
					);

					ImGui::Spacing();
					UI_AdapterInfo();
					UI_D3D12PipelineInfo();
					ImGui::Spacing();

					ImGui::Text("Selection Style: ");
					ImGui::SameLine();

					const char* selectionStyles[] =
					{
						"Blue Pixel Shader",
						"Hidden",
						"None",
					};

					const char* btnLabel = "Clear Selections";
					float buttonWidth = ImGui::CalcTextSize(btnLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
					float spacing = ImGui::GetStyle().ItemSpacing.x;
					float comboWidth = ImGui::GetContentRegionAvail().x - buttonWidth - spacing;
					ImGui::SetNextItemWidth(comboWidth);

					if (ImGui::Combo("##SelectionStyle", &gSelectionStyleIndex, selectionStyles, IM_ARRAYSIZE(selectionStyles)))
					{
						HookD3D12::gShaderSelectionStyle = (HookD3D12::PixelShaderSelectionStyle)gSelectionStyleIndex;
						HookD3D12::ClearShaderMarkers();
						HookD3D12::InvalidateShaderMarkerPSOs();
					}

					ImGui::SameLine();
					if (ImGui::Button(btnLabel))
						HookD3D12::ClearShaderMarkers();

					ImGui::Spacing();

					UI_StreamPipelines();

					//NOTE: hidden from GUI for now, even though the app can largly support operations
					//with the graphics pipeline, for the most part with the game, FF7 rebirth we are primarily
					//going to be messing with the stream pipeline, and this also will help avoid confusion for users
					//UI_GraphicsPipelines();
					ImGui::EndTabItem();
				}

				if (Globals::gRenderDocIntegrationEnabled && ImGui::BeginTabItem("RenderDoc"))
				{
					HookD3D12::ClearShaderMarkers();
					UI_RenderDoc();
					ImGui::EndTabItem();
				}

				ImGui::EndTabBar();
			}

			ImGui::Spacing();
			ImGui::Unindent(indentSpace);
		}
		else
		{
			HookD3D12::ClearShaderMarkers();
		}
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ADAPTER INFO |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ADAPTER INFO |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ADAPTER INFO |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_AdapterInfo()
	{
		if (ImGui::TreeNodeEx("Adapter Info"))
		{
			ImGui::SeparatorText("GPU");
			ImGui::Text("Adapter: %s", HookD3D12::gPipelineInfo.gpuName.c_str());
			ImGui::Text("Vendor ID: 0x%X", HookD3D12::gPipelineInfo.vendorId);
			ImGui::Text("Device ID: 0x%X", HookD3D12::gPipelineInfo.deviceId);
			ImGui::SeparatorText("Memory");
			ImGui::Text("Dedicated VRAM: %.2f GB", HookD3D12::gPipelineInfo.dedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0));
			ImGui::Text("Dedicated System: %.2f GB", HookD3D12::gPipelineInfo.dedicatedSystemMemory / (1024.0 * 1024.0 * 1024.0));
			ImGui::Text("Shared System: %.2f GB", HookD3D12::gPipelineInfo.sharedSystemMemory / (1024.0 * 1024.0 * 1024.0));
			ImGui::Separator();
			ImGui::TreePop();
		}
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| D3D12 PIPELINE INFO |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| D3D12 PIPELINE INFO |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| D3D12 PIPELINE INFO |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_D3D12PipelineInfo()
	{
		if (ImGui::TreeNodeEx("D3D12 Pipeline Info"))
		{
			ImGui::SeparatorText("Swap Chain");
			ImGui::Text("Buffers: %u", HookD3D12::gPipelineInfo.swapChainBuffers);
			ImGui::Text("Format: %u", HookD3D12::gPipelineInfo.swapChainFormat);
			ImGui::SeparatorText("Feature Support");
			ImGui::Text("Resource Binding Tier: %u", HookD3D12::gPipelineInfo.resourceBindingTier);
			ImGui::Text("Tiled Resources Tier: %u", HookD3D12::gPipelineInfo.tiledResourcesTier);
			ImGui::Text("Conservative Raster Tier: %u", HookD3D12::gPipelineInfo.conservativeRasterTier);
			ImGui::Text("Raytracing Tier: %u", HookD3D12::gPipelineInfo.raytracingTier);
			ImGui::Text("Mesh Shader Tier: %u", HookD3D12::gPipelineInfo.meshShaderTier);
			ImGui::SeparatorText("Command Queue");
			ImGui::Text("Queue Type: %u", HookD3D12::gPipelineInfo.commandQueueType);
			ImGui::Separator();
			ImGui::TreePop();
		}
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GRAPHICS PIPELINES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GRAPHICS PIPELINES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GRAPHICS PIPELINES |||||||||||||||||||||||||||||||||||||||||||||||||||||

	/*
	//NOTE: disabled/hidden from the user for now to avoid confusion during setup
	//and also most of the games shader resources/pso goes through the stream pipeline
	//KEEP IT AROUND, DON'T REMOVE AS IT WILL STILL BE USEFUL IN THE FUTURE
	void UI_GraphicsPipelines()
	{
		std::string headerText = "Graphics Pipelines: " + std::to_string(HookD3D12::gGraphicsPipelines.size()) + " PSOs";

		if (ImGui::CollapsingHeader(headerText.c_str()))
		{
			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::psHash, &HookD3D12::GraphicsPipelineInfo::psSize, &HookD3D12::GraphicsPipelineInfo::psBytecode>(
				"Pixel Shaders", "GraphicsPS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::PixelShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, true, true, &HookD3D12::GraphicsPipelineInfo::psDisabled, &HookD3D12::GraphicsPipelineInfo::psoWithoutPS);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::vsHash, &HookD3D12::GraphicsPipelineInfo::vsSize, &HookD3D12::GraphicsPipelineInfo::vsBytecode>(
				"Vertex Shaders", "GraphicsVS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::VertexShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, true, nullptr, nullptr);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::gsHash, &HookD3D12::GraphicsPipelineInfo::gsSize, &HookD3D12::GraphicsPipelineInfo::gsBytecode>(
				"Geometry Shaders", "GraphicsGS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::GeometryShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, true, nullptr, nullptr);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::hsHash, &HookD3D12::GraphicsPipelineInfo::hsSize, &HookD3D12::GraphicsPipelineInfo::hsBytecode>(
				"Hull Shaders", "GraphicsHS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::HullShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, true, nullptr, nullptr);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::dsHash, &HookD3D12::GraphicsPipelineInfo::dsSize, &HookD3D12::GraphicsPipelineInfo::dsBytecode>(
				"Domain Shaders", "GraphicsDS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::DomainShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, true, nullptr, nullptr);
		}
	}
	*/

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STREAM PIPELINES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STREAM PIPELINES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STREAM PIPELINES |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_StreamPipelines()
	{
		std::string headerText = "Stream Pipelines: " + std::to_string(HookD3D12::gPipelineStates.size()) + " PSOs";

		if (ImGui::CollapsingHeader(headerText.c_str()))
		{
			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::psHash, &HookD3D12::PipelineStateInfo::psSize, &HookD3D12::PipelineStateInfo::psBytecode>(
				"Pixel Shaders", "StreamPS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::PixelShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, false, &HookD3D12::PipelineStateInfo::psDisabled, &HookD3D12::PipelineStateInfo::psoWithoutPS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::csHash, &HookD3D12::PipelineStateInfo::csSize, &HookD3D12::PipelineStateInfo::csBytecode>(
				"Compute Shaders", "StreamCS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::ComputeShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, false, &HookD3D12::PipelineStateInfo::csDisabled, &HookD3D12::PipelineStateInfo::psoWithoutCS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::vsHash, &HookD3D12::PipelineStateInfo::vsSize, &HookD3D12::PipelineStateInfo::vsBytecode>(
				"Vertex Shaders", "StreamVS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::VertexShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::vsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutVS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::gsHash, &HookD3D12::PipelineStateInfo::gsSize, &HookD3D12::PipelineStateInfo::gsBytecode>(
				"Geometry Shaders", "StreamGS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::GeometryShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::gsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutGS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::hsHash, &HookD3D12::PipelineStateInfo::hsSize, &HookD3D12::PipelineStateInfo::hsBytecode>(
				"Hull Shaders", "StreamHS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::HullShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::hsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutHS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::dsHash, &HookD3D12::PipelineStateInfo::dsSize, &HookD3D12::PipelineStateInfo::dsBytecode>(
				"Domain Shaders", "StreamDS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::DomainShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::dsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutDS);
		}
		else
		{
			HookD3D12::ClearShaderMarkers();
		}
	}

	//UI "Template" for each of the shader stages
	template<
		typename PipelineT,
		uint64_t PipelineT::* HashMember,
		SIZE_T PipelineT::* SizeMember,
		std::vector<uint8_t> PipelineT::* BytecodeMember>
	void UI_ShaderStageList(
		const char* stageLabel,
		const char* idPrefix,
		const char* sourceList,
		std::vector<PipelineT>& pipelines,
		ShaderTarget::ShaderType shaderType,
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE subobjectType,
		HookD3D12::PSOPendingRebuild::SourceList pendingSource,
		bool allowMarkerToggle,
		bool disableActions,
		bool PipelineT::* disabledMember,
		ID3D12PipelineState* PipelineT::* rebuiltPSOMember)
	{
		static int selectedIndex = -1;
		static int sortMode = 0;

		const int count = CountShaderStage<PipelineT, HashMember>(pipelines);
		std::string stageNodeLabel = std::string(stageLabel) + ": " + std::to_string(count) + "##" + idPrefix;

		if (!ImGui::TreeNodeEx(stageNodeLabel.c_str(), ImGuiTreeNodeFlags_None))
			return;

		if (count == 0)
		{
			ImGui::Text("No shaders captured for this stage.");
			ImGui::TreePop();
			ImGui::Separator();
			return;
		}

		if (selectedIndex < 0 || selectedIndex >= (int)pipelines.size() || !(pipelines[selectedIndex].*HashMember))
			selectedIndex = FindFirstShaderStageIndex<PipelineT, HashMember>(pipelines);

		std::vector<int> sortedIndices;
		sortedIndices.reserve((size_t)count);

		for (int i = 0; i < (int)pipelines.size(); i++)
		{
			if (pipelines[i].*HashMember)
				sortedIndices.push_back(i);
		}

		std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b)
			{
				const PipelineT& left = pipelines[a];
				const PipelineT& right = pipelines[b];

				if (sortMode == 1)
				{
					const SIZE_T leftSize = left.*SizeMember;
					const SIZE_T rightSize = right.*SizeMember;

					if (leftSize != rightSize)
						return leftSize > rightSize;
				}
				else
				{
					const uint64_t leftHash = left.*HashMember;
					const uint64_t rightHash = right.*HashMember;

					if (leftHash != rightHash)
						return leftHash < rightHash;
				}

				return a < b;
			});

		ImGui::Text("Sort By:");
		ImGui::SameLine();
		std::string hashSortLabel = std::string("Hash A-Z##") + idPrefix;
		ImGui::RadioButton(hashSortLabel.c_str(), &sortMode, 0);
		ImGui::SameLine();
		std::string lengthSortLabel = std::string("Bytecode Length##") + idPrefix;
		ImGui::RadioButton(lengthSortLabel.c_str(), &sortMode, 1);

		std::string childLabel = std::string("ShaderList##") + idPrefix;
		if (ImGui::BeginChild(childLabel.c_str(), ImVec2(0, 180), ImGuiChildFlags_Borders))
		{
			for (int sortedIndex : sortedIndices)
			{
				PipelineT& pipeline = pipelines[sortedIndex];
				const uint64_t hash = pipeline.*HashMember;
				const int rowReplacementIndex = HookD3D12::FindEnabledShaderTarget(hash, shaderType);
				const bool hasReplacement = rowReplacementIndex >= 0;
				std::string label = "#" + std::to_string(sortedIndex) + "  " + Hash::FormatHash(hash) + "  (" + std::to_string((size_t)(pipeline.*SizeMember)) + " bytes)##" + idPrefix + std::to_string(sortedIndex);
				const bool isSelected = sortedIndex == selectedIndex;

				if (hasReplacement)
				{
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
				}

				if (ImGui::Selectable(label.c_str(), isSelected || hasReplacement))
				{
					selectedIndex = sortedIndex;

					const HookD3D12::PixelShaderSelectionStyle selectionStyle = (HookD3D12::PixelShaderSelectionStyle)gSelectionStyleIndex;

					if (selectionStyle == HookD3D12::PixelShaderSelectionStyle::None)
					{
						HookD3D12::ClearShaderMarkers();
					}
					else if (allowMarkerToggle && disabledMember && rebuiltPSOMember)
					{
						HookD3D12::gShaderSelectionStyle = selectionStyle;
						HookD3D12::ClearShaderMarkers();
						pipeline.*disabledMember = true;
						HookD3D12::MarkShaderTargetApplyDirty();

						if (!(pipeline.*rebuiltPSOMember))
						{
							HookD3D12::gPendingRebuilds.push_back({
								pendingSource,
								selectedIndex,
								subobjectType
								});
						}
					}
				}

				if (hasReplacement)
					ImGui::PopStyleColor(3);

				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndChild();

		if (selectedIndex < 0 || selectedIndex >= (int)pipelines.size() || !(pipelines[selectedIndex].*HashMember))
		{
			ImGui::TreePop();
			ImGui::Separator();
			return;
		}

		PipelineT& pipeline = pipelines[selectedIndex];
		const uint64_t hash = pipeline.*HashMember;
		const SIZE_T bytecodeSize = pipeline.*SizeMember;
		std::vector<uint8_t>& bytecode = pipeline.*BytecodeMember;

		const int selectedReplacementIndex = HookD3D12::FindEnabledShaderTarget(hash, shaderType);
		const char* selectedReplacementName = selectedReplacementIndex >= 0 ? HookD3D12::gLoadedShaderTargets[selectedReplacementIndex].name.c_str() : "(none)";

		ImGui::SeparatorText("Selected Shader");
		ImGui::Text("Pipeline Index: %d", selectedIndex);
		ImGui::Text("Hash: %s", Hash::FormatHash(hash).c_str());
		ImGui::Text("Bytecode Length: %zu", (size_t)bytecodeSize);
		ImGui::Text("Shader Replacement: %s", selectedReplacementName);
		ImGui::Text("PSO: %p", pipeline.pipelineState);

		if (allowMarkerToggle && disabledMember && rebuiltPSOMember)
		{
			ImGui::Text("Marker: %s", pipeline.*disabledMember ? "active" : "inactive");

			if ((HookD3D12::PixelShaderSelectionStyle)gSelectionStyleIndex == HookD3D12::PixelShaderSelectionStyle::None)
				ImGui::Text("Selection Style is None.");
		}

		if (bytecode.empty())
		{
			ImGui::Text("Bytecode was not captured for this stage.");
			ImGui::TreePop();
			ImGui::Separator();
			return;
		}

		if (!disableActions)
		{
			std::string createTemplateButtonLabel = std::string("Create Modified Shader Template##") + idPrefix;
			if (ImGui::Button(createTemplateButtonLabel.c_str()))
			{
				std::string creationMessage;
				if (ModifiedShaderCreation::CreateTemplate(
					shaderType,
					hash,
					bytecode.data(),
					bytecode.size(),
					creationMessage))
				{
					WriteToRuntimeLogSuccess(creationMessage);
					if (!ShaderAutomaticDiscovery::ProcessCapturedShader(
						pipeline,
						selectedIndex,
						shaderType,
						hash,
						bytecode))
					{
						WriteToRuntimeLogError(
							"Modified Shader was created, but automatic Shader Target creation failed.");
					}
				}
				else
				{
					WriteToRuntimeLogError(creationMessage);
				}

				//clear markers because they can get annoyingly sticky
				HookD3D12::ClearShaderMarkers();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltipCreateModifiedShaderTemplate);

			ImGui::SameLine();

			std::string dumpButtonLabel = std::string("Dump Bytecode##") + idPrefix;
			if (ImGui::Button(dumpButtonLabel.c_str()))
			{
				ShaderInjectorIO::DumpShaderBytecode(bytecode.data(), bytecode.size(), hash, StringHelper::ShaderTypeToString(shaderType), ShaderInjectorIO::GetDumpsDirectory());
			}
		}

		ImGui::TreePop();
		ImGui::Separator();
	}

	template<typename PipelineT, uint64_t PipelineT::* HashMember>
	int CountShaderStage(const std::vector<PipelineT>& pipelines)
	{
		int count = 0;

		for (const auto& pipeline : pipelines)
		{
			if (pipeline.*HashMember)
				count++;
		}

		return count;
	}

	template<typename PipelineT, uint64_t PipelineT::* HashMember>
	int FindFirstShaderStageIndex(const std::vector<PipelineT>& pipelines)
	{
		for (int i = 0; i < (int)pipelines.size(); i++)
		{
			if (pipelines[i].*HashMember)
				return i;
		}

		return -1;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RUNTIME LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RUNTIME LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RUNTIME LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void WriteToRuntimeLog(std::string text)
	{
		{
			std::lock_guard<std::mutex> lock(gRuntimeLogMutex);
			runtimeLogText += "\n";
			runtimeLogText += text;
		}
		ShaderInjectorIO::WriteToLogFile(text);
	}

	void WriteToRuntimeLogError(std::string text)
	{
		WriteToRuntimeLog("[ERROR] " + text);
	}

	void WriteToRuntimeLogSuccess(std::string text)
	{
		WriteToRuntimeLog("[SUCCESS] " + text);
	}

	void WriteToRuntimeLogWarning(std::string text)
	{
		WriteToRuntimeLog("[WARNING] " + text);
	}

	void ClearRuntimeLog()
	{
		std::lock_guard<std::mutex> lock(gRuntimeLogMutex);
		runtimeLogText = "";
	}

	std::string GetRuntimeLogSnapshot()
	{
		std::lock_guard<std::mutex> lock(gRuntimeLogMutex);
		return runtimeLogText;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STYLE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STYLE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STYLE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_ApplyStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style = ImGuiStyle();
		ImGui::StyleColorsDark(&style);

		//cool rounding and spacing!
		style.WindowRounding = 6.0f;
		style.FrameRounding = 4.0f;
		style.ScrollbarRounding = 4.0f;
		style.GrabRounding = 4.0f;
		style.TabRounding = 4.0f;
		style.WindowPadding = ImVec2(10.0f, 10.0f);
		style.FramePadding = ImVec2(6.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ScrollbarSize = 12.0f;

		ImVec4* c = style.Colors;

		//accents for hover / active / grab
		const ImVec4 accent = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
		const ImVec4 accentHovered = ImVec4(0.35f, 0.55f, 0.80f, 1.00f);
		const ImVec4 accentActive = ImVec4(0.20f, 0.38f, 0.62f, 1.00f);

		//text
		c[ImGuiCol_Text] = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
		c[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

		//window, background surfaces
		c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
		c[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
		c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

		//borders
		c[ImGuiCol_Border] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

		//title bar
		c[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		c[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.08f, 0.75f);

		//frames (inputText, sliders)
		c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
		c[ImGuiCol_FrameBgHovered] = accentHovered;
		c[ImGuiCol_FrameBgActive] = accentActive;

		//buttons
		c[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		c[ImGuiCol_ButtonHovered] = accentHovered;
		c[ImGuiCol_ButtonActive] = accentActive;

		//headers (collapsingHeader, selectable, treeNode)
		c[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		c[ImGuiCol_HeaderHovered] = accentHovered;
		c[ImGuiCol_HeaderActive] = accentActive;

		//tabs ---
		c[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
		c[ImGuiCol_TabHovered] = accentHovered;
		c[ImGuiCol_TabActive] = accent;
		c[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

		//checkmark / slider grab
		c[ImGuiCol_CheckMark] = accentHovered;
		c[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
		c[ImGuiCol_SliderGrabActive] = accentActive;

		//scrollbar
		c[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
		c[ImGuiCol_ScrollbarGrabHovered] = accentHovered;
		c[ImGuiCol_ScrollbarGrabActive] = accentActive;

		//resize grip
		c[ImGuiCol_ResizeGrip] = ImVec4(0.22f, 0.22f, 0.22f, 0.50f);
		c[ImGuiCol_ResizeGripHovered] = accentHovered;
		c[ImGuiCol_ResizeGripActive] = accentActive;

		//separator
		c[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
		c[ImGuiCol_SeparatorHovered] = accentHovered;
		c[ImGuiCol_SeparatorActive] = accentActive;

		//other
		c[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
		c[ImGuiCol_NavHighlight] = accent;

		//Rebuild the style from an unscaled baseline every frame so live scale changes never
		//compound the previous frame's dimensions.
		const float menuScale = (std::clamp)(Globals::gShaderInjectorGUIScale, 0.5f, 4.0f);
		style.ScaleAllSizes(menuScale);
		style.FontScaleMain = menuScale;
	}
}
