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
#include "RenderPass/RenderPassShaderCompileQueue.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/ConfigurationProgress.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"
#include "ModifiedShader/ModifiedShaderCreation.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "RenderPass/RenderPassRuntime.h"
#include "RenderPass/RenderPassConfigurationProperties.h"
#include "ShaderDiscovery/ShaderAutomaticDiscovery.h"
#include "StringHelper.h"
#include "GUI/ShaderInjectorGUITooltips.h"
#include "Input/Keycodes.h"
#include "ShaderInjectorVersion.h"

namespace ShaderInjectorGUI
{
	static char gShaderConfigurationSearch[256]{};
	static char gPresetName[128]{};
	static char gCategoryName[128]{};
	static char gRenderPassPropertyName[128]{};
	static char gRenderPassPropertyDescription[256]{};
	static char gRenderPassPropertyDefaultValue[32] = "true";
	static char gConfigurationManagementMessage[512]{};
	static char gSelectedPropertyName[128]{};
	static char gSelectedPropertyDescription[256]{};
	static bool gShaderConfigurationDirty = false;
	static bool gShaderConfigurationNeedsApply = false;
	static bool gDeletePresetConfirmationOpen = false;
	static std::string gPresetIDPendingDelete;
	static std::string gSelectedShaderConfigurationPropertyID;
	static std::string gEditingShaderConfigurationPropertyID;
	static double gShaderConfigurationFinishedTime = -1.0;

	bool ShaderConfigurationOperationIsRunning()
	{
		const auto progress = DatabaseShaderConfigurations::GetOperationProgressTracker();

		return progress && progress->GetSnapshot().blocksEditing;
	}

	bool CancelBackgroundCompilationForEdit()
	{
		const auto progress = DatabaseShaderConfigurations::GetOperationProgressTracker();

		if (!progress)
			return true;

		const ShaderConfiguration::ConfigurationProgressSnapshot snapshot = progress->GetSnapshot();

		if (snapshot.finished || snapshot.cancellationRequested)
			return true;

		if (snapshot.blocksEditing)
			return false;

		const bool canceled = DatabaseShaderConfigurations::CancelBackgroundCompilation();

		if (canceled)
			gShaderConfigurationFinishedTime = -1.0;

		return canceled;
	}

	void MarkShaderConfigurationEdited(bool markDirty = true, bool needsApply = true)
	{
		CancelBackgroundCompilationForEdit();
		gShaderConfigurationFinishedTime = -1.0;

		if (markDirty)
			gShaderConfigurationDirty = true;

		if (needsApply)
			gShaderConfigurationNeedsApply = true;
	}

	void ProcessShaderConfigurationOperation()
	{
		const auto operationResult = DatabaseShaderConfigurations::ProcessCompletedOperation();

		if (!operationResult.completed)
			return;

		if (!operationResult.succeeded)
		{
			WriteToRuntimeLogError("Shader Configuration operation failed: " + operationResult.errorMessage);
			return;
		}

		gShaderConfigurationDirty = false;
		const auto progress = DatabaseShaderConfigurations::GetOperationProgressTracker();

		if (operationResult.applying)
		{
			gShaderConfigurationNeedsApply = false;
			DatabaseRenderPasses::PublishRuntimeConfiguration(true);

			//keep this operation open until its shaders have been compiled and published.
			progress->BeginCompiling();
			const bool includeInactivePackages = operationResult.forceRecompileAll;
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration = operationResult.previousConfiguration;

			if (operationResult.forceRecompileAll)
				previousConfiguration.reset();

			//the pass queue applies its own dependency filter and owns its target-variant compilation.
			const auto modifiedShaderResult = RecompileModifiedShaders(includeInactivePackages, previousConfiguration, false);
			const auto renderPassResult = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles(previousConfiguration);
			const size_t failedRequests = static_cast<size_t>(modifiedShaderResult.failedPackageCount) + renderPassResult.failedRenderPassCount;

			//a rejected request still counts as finished work, with a visible failure.
			for (size_t failureIndex = 0; failureIndex < failedRequests; ++failureIndex)
			{
				const auto failedTask = progress->BeginTask();
				failedTask->MarkFailed();
			}

			for (const std::string& error : renderPassResult.errors)
				WriteToRuntimeLogError(error);

			WriteToRuntimeLogSuccess("Applied Shader Configuration properties=" + std::to_string(operationResult.applyResult.propertyCount));
		}
		else
			WriteToRuntimeLogSuccess("Reloaded Shader Configuration properties.");

		//render-pass jobs can add their target variants before their own task finishes.
		progress->FinishDispatch();
	}

	void DrawShaderConfigurationProgress()
	{
		const auto tracker = DatabaseShaderConfigurations::GetOperationProgressTracker();

		if (!tracker)
			return;

		const auto progress = tracker->GetSnapshot();
		float fraction = 0.0f;

		if (progress.total > 0)
			fraction = static_cast<float>(progress.completed) / static_cast<float>(progress.total);

		std::string overlay = progress.stage;
		const bool priorityComplete = progress.dispatchFinished && !progress.cancellationRequested && progress.priorityTotal > 0 &&
			progress.priorityCompleted >= progress.priorityTotal &&
			progress.priorityFailed == 0;
		const size_t displayedPriorityCompleted = (std::min)(progress.priorityCompleted, progress.priorityTotal);

		if (priorityComplete && !progress.finished)
			overlay = "Active shaders ready; compiling remaining shaders " + std::to_string(progress.completed) + "/" + std::to_string(progress.total);
		else if (progress.dispatchFinished && progress.priorityTotal > 0 && !progress.finished)
			overlay = "Compiling active shaders " + std::to_string(displayedPriorityCompleted) + "/" + std::to_string(progress.priorityTotal) +
				" (" + std::to_string(progress.completed) + "/" + std::to_string(progress.total) + " total)";

		if (progress.total > 0 && progress.priorityTotal == 0)
			overlay += " " + std::to_string(progress.completed) + "/" + std::to_string(progress.total);

		if (progress.cancellationRequested)
		{
			if (gShaderConfigurationFinishedTime < 0.0)
				gShaderConfigurationFinishedTime = ImGui::GetTime();

			if (ImGui::GetTime() - gShaderConfigurationFinishedTime > 3.0)
				return;

			fraction = 1.0f;
			overlay = "Background compilation canceled; apply changes to restart";
		}
		else if (progress.finished)
		{
			if (gShaderConfigurationFinishedTime < 0.0)
				gShaderConfigurationFinishedTime = ImGui::GetTime();

			if (ImGui::GetTime() - gShaderConfigurationFinishedTime > 3.0)
				return;

			fraction = 1.0f;
			overlay = "Shader Configuration complete";

			if (progress.failed > 0 || !progress.errorMessage.empty())
				overlay = "Shader Configuration finished with errors";
		}
		else
			gShaderConfigurationFinishedTime = -1.0;

		if (progress.failed > 0)
			overlay += " (" + std::to_string(progress.failed) + " failed)";

		if (progress.canceled > 0)
			overlay += " (" + std::to_string(progress.canceled) + " superseded)";

		//scope the completed fill color to this bar so other progress bars keep their theme.
		const bool progressIsGreen = priorityComplete;

		if (progressIsGreen)
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));

		ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), overlay.c_str());

		if (progressIsGreen)
			ImGui::PopStyleColor();

		if (ImGui::IsItemHovered())
		{
			if (!progress.errorMessage.empty())
				ImGui::SetTooltip("%s", progress.errorMessage.c_str());
			else
				ImGui::SetTooltip("Progress includes saving settings, compiling shaders, and loading their replacements. Current shaders remain active while their replacements are prepared.");
		}
	}

	bool ShaderConfigurationPropertyMatchesSearch(const ShaderConfiguration::PropertyDisk& property, const std::string& lowercaseSearch)
	{
		if (lowercaseSearch.empty())
			return true;

		const std::string searchableText = StringHelper::LowercaseAscii(
			property.name + "\n" +
			property.sourceFile + "\n" +
			property.sourcePath + "\n" +
			property.description + "\n" +
			property.category);

		return searchableText.find(lowercaseSearch) != std::string::npos;
	}

	std::vector<std::string> ShaderConfigurationCategoryNames(const ShaderConfiguration::DocumentDisk& document)
	{
		std::vector<std::string> categories{ "Uncategorized" };

		for (const ShaderConfiguration::ShaderConfigurationCategory& category : document.categories)
		{
			if (category.name != "Uncategorized")
				categories.push_back(category.name);
		}

		//keep category groups and their selectors in the same alphabetical order.
		std::stable_sort(categories.begin(), categories.end(), [](const std::string& firstCategory, const std::string& secondCategory)
		{
			return StringHelper::LowercaseAscii(firstCategory) < StringHelper::LowercaseAscii(secondCategory);
		});

		return categories;
	}

	void SetConfigurationManagementMessage(const std::string& message);

	void DrawShaderConfigurationCategorySelector(ShaderConfiguration::PropertyDisk& property, const std::vector<std::string>& categories)
	{
		PropertyWidgets::PrepareRow("Category");

		if (ImGui::BeginCombo("##PropertyCategory", property.category.c_str()))
		{
			for (const std::string& category : categories)
			{
				const bool selected = property.category == category;

				if (ImGui::Selectable(category.c_str(), selected))
				{
					if (selected)
						continue;

					std::string error;

					if (DatabaseShaderConfigurations::SetPropertyCategory(property.id, category, error))
					{
						MarkShaderConfigurationEdited(false, false);
						SetConfigurationManagementMessage("");
					}
					else
						SetConfigurationManagementMessage(error);
				}

				if (selected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}
	}

	void SetConfigurationManagementMessage(const std::string& message)
	{
		std::snprintf(gConfigurationManagementMessage, sizeof(gConfigurationManagementMessage), "%s", message.c_str());
	}

	bool ApplyShaderConfigurationPropertyChanges(bool forceRecompileAll = false)
	{
		std::string validationError;

		if (!RenderPass::ValidateConfigurationProperties(DatabaseShaderConfigurations::GetEditableDocument(), DatabaseRenderPasses::GetRenderPasses(), validationError))
		{
			SetConfigurationManagementMessage(validationError);
			return false;
		}

		if (!DatabaseShaderConfigurations::QueueApplyChanges(forceRecompileAll))
			return false;

		gShaderConfigurationFinishedTime = -1.0;
		SetConfigurationManagementMessage("");
		return true;
	}

	bool SelectShaderConfigurationPreset(const std::string& presetID)
	{
		if (gShaderConfigurationDirty)
		{
			SetConfigurationManagementMessage("Save the current preset before switching presets.");
			return false;
		}

		if (DatabaseShaderConfigurations::GetEditableDocument().presetID == presetID)
			return true;

		if (!CancelBackgroundCompilationForEdit())
			return false;

		std::string error;

		if (!DatabaseShaderConfigurations::SelectPreset(presetID, error))
		{
			SetConfigurationManagementMessage(error);
			return false;
		}

		//preset selection uses the same validation, background apply, and progress as the button.
		MarkShaderConfigurationEdited(false, true);
		return ApplyShaderConfigurationPropertyChanges();
	}

	std::string GetShaderConfigurationPresetName(const ShaderConfiguration::DocumentDisk& document)
	{
		for (const ShaderConfiguration::PresetDisk& preset : DatabaseShaderConfigurations::GetPresets())
		{
			if (preset.id == document.presetID)
				return preset.name;
		}

		return "Unknown preset";
	}

	void DrawShaderConfigurationManagement(ShaderConfiguration::DocumentDisk& document, const std::vector<std::string>& categories)
	{
		const bool operationRunning = ShaderConfigurationOperationIsRunning();
		const std::vector<ShaderConfiguration::PresetDisk>& presets = DatabaseShaderConfigurations::GetPresets();
		const std::string activePresetName = GetShaderConfigurationPresetName(document);

		ImGui::BeginDisabled(operationRunning);
		PropertyWidgets::PrepareRow("Active preset");
		const float activePresetWidth = ImGui::GetContentRegionAvail().x;
		const float savePresetWidth = ImGui::CalcTextSize("Save Preset").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth((std::max)(1.0f, activePresetWidth - savePresetWidth - ImGui::GetStyle().ItemSpacing.x));

		if (ImGui::BeginCombo("##ActivePresetShaderConfiguration", activePresetName.c_str()))
		{
			for (const ShaderConfiguration::PresetDisk& preset : presets)
			{
				const bool selected = preset.id == document.presetID;

				if (ImGui::Selectable(preset.name.c_str(), selected) && !selected)
					SelectShaderConfigurationPreset(preset.id);

				if (selected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(operationRunning || !gShaderConfigurationDirty);
		if (ImGui::Button("Save Preset##ShaderConfiguration"))
		{
			std::string error;
			if (DatabaseShaderConfigurations::SavePreset(error))
			{
				gShaderConfigurationDirty = false;
				SetConfigurationManagementMessage("Preset saved.");
			}
			else
				SetConfigurationManagementMessage(error);
		}
		ImGui::EndDisabled();

		if (ImGui::TreeNode("Preset##ShaderConfiguration"))
		{
			PropertyWidgets::PrepareRow("Preset name");
			ImGui::InputTextWithHint("##PresetName", "Name for create or rename", gPresetName, sizeof(gPresetName));
			const float presetActionWidth = (std::max)(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f);
			ImGui::BeginDisabled(gShaderConfigurationDirty);

			if (ImGui::Button("Create##ShaderConfigurationPreset", ImVec2(presetActionWidth, 0)))
			{
				std::string error;

				if (DatabaseShaderConfigurations::CreatePreset(gPresetName, error))
				{
					gPresetName[0] = '\0';
					MarkShaderConfigurationEdited(false, true);
					gShaderConfigurationDirty = false;
					SetConfigurationManagementMessage("");
				}
				else
					SetConfigurationManagementMessage(error);
			}

			ImGui::SameLine();

			if (ImGui::Button("Rename##ShaderConfigurationPreset", ImVec2(presetActionWidth, 0)))
			{
				std::string error;

				if (DatabaseShaderConfigurations::RenamePreset(document.presetID, gPresetName, error))
				{
					gPresetName[0] = '\0';
					SetConfigurationManagementMessage("");
				}
				else
					SetConfigurationManagementMessage(error);
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete##ShaderConfigurationPreset", ImVec2(presetActionWidth, 0)))
			{
				gPresetIDPendingDelete = document.presetID;
				gDeletePresetConfirmationOpen = true;
			}

			ImGui::EndDisabled();

			ImGui::SameLine();

			if (ImGui::Button("Open Folder##ShaderConfigurationPresets", ImVec2(presetActionWidth, 0)) && !ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetShaderConfigurationDirectory()))
				WriteToRuntimeLogError("Could not open the Shader Configuration folder.");
			ImGui::TreePop();
		}

		if (ImGui::BeginPopupModal("Delete Shader Configuration Preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped("Delete this preset and its saved values?");

			if (ImGui::Button("Delete Preset"))
			{
				std::string error;

				if (DatabaseShaderConfigurations::DeletePreset(gPresetIDPendingDelete, error))
				{
					MarkShaderConfigurationEdited(false, true);
					gShaderConfigurationDirty = false;
					SetConfigurationManagementMessage("");
				}
				else
					SetConfigurationManagementMessage(error);

				gDeletePresetConfirmationOpen = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel##DeletePreset"))
			{
				gDeletePresetConfirmationOpen = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (gDeletePresetConfirmationOpen)
			ImGui::OpenPopup("Delete Shader Configuration Preset?");

		ImGui::EndDisabled();
		if (gConfigurationManagementMessage[0] != '\0')
			ImGui::TextWrapped("%s", gConfigurationManagementMessage);
	}

	void DrawShaderConfigurationCategories(const std::vector<std::string>& categories)
	{
		const bool operationRunning = ShaderConfigurationOperationIsRunning();
		ImGui::BeginDisabled(operationRunning);
		static int selectedCategoryIndex = 0;
		if (selectedCategoryIndex >= static_cast<int>(categories.size()))
			selectedCategoryIndex = 0;

		PropertyWidgets::PrepareRow("Recognized category");

		if (ImGui::BeginCombo("##ManageCategory", categories[selectedCategoryIndex].c_str()))
		{
			for (size_t categoryIndex = 0; categoryIndex < categories.size(); ++categoryIndex)
			{
				if (ImGui::Selectable(categories[categoryIndex].c_str(), selectedCategoryIndex == static_cast<int>(categoryIndex)))
				{
					selectedCategoryIndex = static_cast<int>(categoryIndex);
					std::snprintf(gCategoryName, sizeof(gCategoryName), "%s", categories[categoryIndex].c_str());
				}
			}

			ImGui::EndCombo();
		}

		PropertyWidgets::PrepareRow("Category name");
		ImGui::InputTextWithHint("##NewCategoryName", "Name for add or rename", gCategoryName, sizeof(gCategoryName));

		const float categoryActionWidth = (std::max)(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f);

		if (ImGui::Button("Add##ShaderConfigurationCategory", ImVec2(categoryActionWidth, 0)))
		{
			std::string error;

			if (DatabaseShaderConfigurations::AddCategory(gCategoryName, error))
			{
				MarkShaderConfigurationEdited(false, false);
				gCategoryName[0] = '\0';
				SetConfigurationManagementMessage("");
			}
			else
				SetConfigurationManagementMessage(error);
		}

		ImGui::SameLine();

		if (ImGui::Button("Rename##ShaderConfigurationCategory", ImVec2(categoryActionWidth, 0)))
		{
			std::string error;

			if (categories[selectedCategoryIndex] == "Uncategorized")
				SetConfigurationManagementMessage("The Uncategorized category cannot be renamed.");
			else if (DatabaseShaderConfigurations::RenameCategory(categories[selectedCategoryIndex], gCategoryName, error))
			{
				MarkShaderConfigurationEdited(false, false);
				gCategoryName[0] = '\0';
				SetConfigurationManagementMessage("");
			}
			else
				SetConfigurationManagementMessage(error);
		}

		ImGui::SameLine();

		if (ImGui::Button("Remove##ShaderConfigurationCategory", ImVec2(categoryActionWidth, 0)))
		{
			std::string error;

			if (DatabaseShaderConfigurations::RemoveCategory(categories[selectedCategoryIndex], error))
			{
				MarkShaderConfigurationEdited(false, false);
				SetConfigurationManagementMessage("");
			}
			else
				SetConfigurationManagementMessage(error);
		}

		ImGui::EndDisabled();

	}

	bool DrawShaderConfigurationProperty(ShaderConfiguration::PropertyDisk& property, bool operationRunning)
	{
		bool changed = false;
		ImGui::PushID(property.id.c_str());
		std::string helpText = property.description;

		if (helpText.empty())
			helpText = "No description was provided.";

		helpText += "\n\nType: " + property.valueType;
		helpText += "\nDefault: " + property.defaultValue;

		if (!property.valueRange.empty())
			helpText += "\nRange: " + property.valueRange;

		helpText += "\nPath: " + property.sourcePath;

		if (PropertyWidgets::PrepareSelectableRow(property.name.c_str(), helpText.c_str(), gSelectedShaderConfigurationPropertyID == property.id))
			gSelectedShaderConfigurationPropertyID = property.id;

		ImGui::BeginDisabled(operationRunning);

		if (property.valueType == "bool")
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
		else if (property.valueType == "int")
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
			const bool hasRange = ShaderConfiguration::TryGetRange(property, minimum, maximum);
			bool valueChanged = false;

			if (hasRange)
				valueChanged = ImGui::SliderInt("##Value", &value, static_cast<int>(minimum), static_cast<int>(maximum));
			else
				valueChanged = ImGui::DragInt("##Value", &value, 1.0f);

			if (valueChanged)
			{
				ShaderConfiguration::SetInteger(property, value);
				changed = true;
			}
		}
		else
		{
			const size_t componentCount = ShaderConfiguration::ComponentCount(property);
			std::vector<float> parsedValues;

			if (!ShaderConfiguration::TryGetFloatComponents(property, parsedValues))
			{
				ShaderConfiguration::PropertyDisk defaultProperty = property;
				defaultProperty.value = property.defaultValue;
				ShaderConfiguration::TryGetFloatComponents(defaultProperty, parsedValues);
			}

			float values[4]{};

			for (size_t index = 0; index < componentCount && index < parsedValues.size(); ++index)
			{
				values[index] = parsedValues[index];
			}

			float minimum = 0.0f;
			float maximum = 0.0f;
			const bool hasRange = ShaderConfiguration::TryGetRange(property, minimum, maximum);
			float dragSpeed = 0.01f;

			if (hasRange)
				dragSpeed = (std::max)(0.001f, (maximum - minimum) / 200.0f);

			bool valueChanged = false;

			switch (componentCount)
			{
				case 1:
					if (hasRange)
						valueChanged = ImGui::SliderFloat("##Value", values, minimum, maximum);
					else
						valueChanged = ImGui::DragFloat("##Value", values, dragSpeed);
					break;
				case 2:
					if (hasRange)
						valueChanged = ImGui::SliderFloat2("##Value", values, minimum, maximum);
					else
						valueChanged = ImGui::DragFloat2("##Value", values, dragSpeed);
					break;
				case 3:
					if (hasRange)
						valueChanged = ImGui::SliderFloat3("##Value", values, minimum, maximum);
					else
						valueChanged = ImGui::DragFloat3("##Value", values, dragSpeed);
					break;
				case 4:
					if (hasRange)
						valueChanged = ImGui::SliderFloat4("##Value", values, minimum, maximum);
					else
						valueChanged = ImGui::DragFloat4("##Value", values, dragSpeed);
					break;
				default:
					ImGui::TextDisabled("Unsupported configuration type: %s", property.valueType.c_str());
					break;
			}

			if (valueChanged)
			{
				ShaderConfiguration::SetFloatComponents(property, values, componentCount);

				changed = true;
			}
		}

		ImGui::EndDisabled();
		ImGui::PopID();
		return changed;
	}

	bool SupportsConfigurationDownscale(const std::vector<std::string>& targetIDs, int factor)
	{
		const auto& renderPasses = DatabaseRenderPasses::GetRenderPasses();

		//a grouped setting offers only factors supported by every selected pass.
		for (const std::string& targetID : targetIDs)
		{
			const auto renderPass = std::find_if(renderPasses.begin(), renderPasses.end(), [&](const auto& candidate) { return candidate.id == targetID; });

			if (renderPass == renderPasses.end())
				return false;

			if (renderPass->type == RenderPass::RenderPassType::DownsampleUpsample)
			{
				if (factor < 2 || factor > 1024)
					return false;
			}
			else if (factor > 16)
				return false;
		}

		return !targetIDs.empty();
	}

	bool DrawRenderPassConfigurationProperty(ShaderConfiguration::PropertyDisk& property, bool operationRunning)
	{
		bool changed = false;
		ImGui::PushID(property.id.c_str());
		const std::string& helpText = property.description;

		std::vector<std::string> targetIDs = property.renderPassIDs;
		if (targetIDs.empty() && !property.renderPassID.empty())
			targetIDs.push_back(property.renderPassID);
		if (PropertyWidgets::PrepareSelectableRow(property.name.c_str(), helpText.c_str(), gSelectedShaderConfigurationPropertyID == property.id))
			gSelectedShaderConfigurationPropertyID = property.id;

		ImGui::BeginDisabled(operationRunning);

		if (ShaderConfiguration::IsEnabledRenderPassProperty(property.renderPassPropertyType))
		{
			bool enabled = property.value != "false" && property.value != "0";

			if (ImGui::Checkbox("##RenderPassEnabled", &enabled))
			{
				property.value = "false";

				if (enabled)
					property.value = "true";
				changed = true;
			}
		}
		else
		{
			int downscaleValue = 1;
			ShaderConfiguration::TryGetInteger(property, downscaleValue);
			ImGui::SetNextItemWidth(100.0f);

			if (ImGui::BeginCombo("##RenderPassDownscale", (std::to_string(downscaleValue) + "x").c_str()))
			{
				for (int value = 1; value <= 1024; value *= 2)
				{
					if (!SupportsConfigurationDownscale(targetIDs, value))
						continue;

					const std::string label = std::to_string(value) + "x";

					if (ImGui::Selectable(label.c_str(), downscaleValue == value))
					{
						property.value = std::to_string(value);
						changed = true;
					}
				}

				ImGui::EndCombo();
			}
		}

		ImGui::EndDisabled();
		ImGui::PopID();
		return changed;
	}

	void DrawAddRenderPassProperty(const std::vector<std::string>& categories)
	{
		static int propertyTypeIndex = 0;
		static int categoryIndex = 0;
		static int defaultDownscaleIndex = 0;
		static std::vector<std::string> selectedRenderPassIDs;
		const std::vector<RenderPass::RenderPassDisk>& renderPasses = DatabaseRenderPasses::GetRenderPasses();

		if (selectedRenderPassIDs.empty() && !renderPasses.empty())
			selectedRenderPassIDs.push_back(renderPasses.front().id);

		if (categoryIndex >= static_cast<int>(categories.size()))
			categoryIndex = 0;

		ImGui::SeparatorText("Injector Framework Property");

		PropertyWidgets::PrepareRow("Name");
		ImGui::InputText("##CustomRenderPassName", gRenderPassPropertyName, sizeof(gRenderPassPropertyName));

		PropertyWidgets::PrepareRow("Description");
		ImGui::InputText("##CustomRenderPassDescription", gRenderPassPropertyDescription, sizeof(gRenderPassPropertyDescription));

		PropertyWidgets::PrepareRow("Property type");
		const char* propertyTypeName = "Render Pass Enable (Single)";
		const char* propertyTypeNames[] = {
			"Render Pass Enable (Single)",
			"Render Pass Enable (Multiple)",
			"Render Pass Downscale Factor (Single)",
			"Render Pass Downscale Factor (Multiple)"};
		propertyTypeName = propertyTypeNames[propertyTypeIndex];
		bool multipleProperty = propertyTypeIndex == 1 || propertyTypeIndex == 3;

		if (ImGui::BeginCombo("##CustomRenderPassType", propertyTypeName))
		{
			for (int index = 0; index < 4; ++index)
			{
				if (!ImGui::Selectable(propertyTypeNames[index], propertyTypeIndex == index))
					continue;

				propertyTypeIndex = index;
				if (index == 0 || index == 1)
					std::snprintf(gRenderPassPropertyDefaultValue, sizeof(gRenderPassPropertyDefaultValue), "true");
				else
					std::snprintf(gRenderPassPropertyDefaultValue, sizeof(gRenderPassPropertyDefaultValue), "1");

				multipleProperty = index == 1 || index == 3;
			}

			ImGui::EndCombo();
		}

		selectedRenderPassIDs.erase(
			std::remove_if(selectedRenderPassIDs.begin(), selectedRenderPassIDs.end(), [&renderPasses](const std::string& selectedID)
				{ return std::none_of(renderPasses.begin(), renderPasses.end(), [&selectedID](const RenderPass::RenderPassDisk& renderPass) { return renderPass.id == selectedID; }); }),
			selectedRenderPassIDs.end());

		if (!multipleProperty && selectedRenderPassIDs.size() > 1)
			selectedRenderPassIDs.resize(1);

		if (selectedRenderPassIDs.empty() && !renderPasses.empty())
			selectedRenderPassIDs.push_back(renderPasses.front().id);

		std::string renderPassLabel = "No render passes available";
		if (multipleProperty && !selectedRenderPassIDs.empty())
			renderPassLabel = std::to_string(selectedRenderPassIDs.size()) + " render passes selected";
		else if (!selectedRenderPassIDs.empty())
		{
			const auto selectedPass = std::find_if(renderPasses.begin(), renderPasses.end(), [](const RenderPass::RenderPassDisk& renderPass)
				{ return renderPass.id == selectedRenderPassIDs.front(); });
			if (selectedPass != renderPasses.end())
				renderPassLabel = selectedPass->name + " (" + selectedPass->id + ")";
		}

		PropertyWidgets::PrepareRow("Render pass");

		if (ImGui::BeginCombo("##CustomRenderPassBinding", renderPassLabel.c_str()))
		{
			for (const RenderPass::RenderPassDisk& renderPass : renderPasses)
			{
				const std::string label = renderPass.name + " (" + renderPass.id + ")";
				const bool selected = std::find(selectedRenderPassIDs.begin(), selectedRenderPassIDs.end(), renderPass.id) != selectedRenderPassIDs.end();

				if (multipleProperty && ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_DontClosePopups))
				{
					if (selected)
						selectedRenderPassIDs.erase(std::remove(selectedRenderPassIDs.begin(), selectedRenderPassIDs.end(), renderPass.id), selectedRenderPassIDs.end());
					else
						selectedRenderPassIDs.push_back(renderPass.id);
				}
				else if (!multipleProperty && ImGui::Selectable(label.c_str(), selected))
				{
					selectedRenderPassIDs = {renderPass.id};
				}
			}

			ImGui::EndCombo();
		}

		PropertyWidgets::PrepareRow("Category");

		if (ImGui::BeginCombo("##CustomRenderPassCategory", categories[categoryIndex].c_str()))
		{
			for (size_t index = 0; index < categories.size(); ++index)
			{
				if (ImGui::Selectable(categories[index].c_str(), categoryIndex == static_cast<int>(index)))
					categoryIndex = static_cast<int>(index);
			}

			ImGui::EndCombo();
		}

		if (propertyTypeIndex < 2)
		{
			bool defaultEnabled = gRenderPassPropertyDefaultValue[0] != 'f';

			PropertyWidgets::PrepareRow("Default enabled");

			if (ImGui::Checkbox("##CustomRenderPassDefaultEnabled", &defaultEnabled))
			{
				std::snprintf(gRenderPassPropertyDefaultValue, sizeof(gRenderPassPropertyDefaultValue), "false");

				if (defaultEnabled)
					std::snprintf(gRenderPassPropertyDefaultValue, sizeof(gRenderPassPropertyDefaultValue), "true");
			}
		}
		else
		{
			//changing targets can invalidate a previously selected default.
			if (!SupportsConfigurationDownscale(selectedRenderPassIDs, 1 << defaultDownscaleIndex))
			{
				for (int factorIndex = 0; factorIndex <= 10; ++factorIndex)
				{
					if (!SupportsConfigurationDownscale(selectedRenderPassIDs, 1 << factorIndex))
						continue;

					defaultDownscaleIndex = factorIndex;
					std::snprintf(gRenderPassPropertyDefaultValue, sizeof(gRenderPassPropertyDefaultValue), "%d", 1 << factorIndex);
					break;
				}
			}

			PropertyWidgets::PrepareRow("Default downscale");

			if (ImGui::BeginCombo("##CustomRenderPassDefaultDownscale", (std::to_string(1 << defaultDownscaleIndex) + "x").c_str()))
			{
				for (int factorIndex = 0; factorIndex <= 10; ++factorIndex)
				{
					const int factor = 1 << factorIndex;

					if (!SupportsConfigurationDownscale(selectedRenderPassIDs, factor))
						continue;

					if (ImGui::Selectable((std::to_string(factor) + "x").c_str(), defaultDownscaleIndex == factorIndex))
					{
						defaultDownscaleIndex = factorIndex;
						std::snprintf(gRenderPassPropertyDefaultValue, sizeof(gRenderPassPropertyDefaultValue), "%d", factor);
					}
				}

				ImGui::EndCombo();
			}
		}

		bool validTargetCount = selectedRenderPassIDs.size() == 1;
		if (multipleProperty)
			validTargetCount = selectedRenderPassIDs.size() > 1;
		if (multipleProperty && selectedRenderPassIDs.size() < 2)
			ImGui::TextDisabled("Select at least two render passes for a multiple property.");
		ImGui::BeginDisabled(!validTargetCount);

		if (ImGui::Button("Add property##CustomRenderPassProperty"))
		{
			std::string error;
			const ShaderConfiguration::RenderPassPropertyType propertyTypes[] = {
				ShaderConfiguration::RenderPassPropertyType::Enabled,
				ShaderConfiguration::RenderPassPropertyType::EnabledMultiple,
				ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwo,
				ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwoMultiple};
			const ShaderConfiguration::RenderPassPropertyType propertyType = propertyTypes[propertyTypeIndex];

			if (DatabaseShaderConfigurations::AddRenderPassProperty(gRenderPassPropertyName, selectedRenderPassIDs, propertyType, gRenderPassPropertyDefaultValue, gRenderPassPropertyDescription, categories[categoryIndex], error))
			{
				gRenderPassPropertyName[0] = '\0';
				gRenderPassPropertyDescription[0] = '\0';
				MarkShaderConfigurationEdited();
				SetConfigurationManagementMessage("");
			}
			else
				SetConfigurationManagementMessage(error);
		}

		ImGui::EndDisabled();
	}
	ShaderConfiguration::PropertyDisk* FindShaderConfigurationPropertyByID(ShaderConfiguration::DocumentDisk& document, const std::string& propertyID)
	{
		for (ShaderConfiguration::PropertyDisk& property : document.properties)
		{
			if (property.id == propertyID)
				return &property;
		}

		return nullptr;
	}

	void DrawSelectedShaderConfigurationProperty(ShaderConfiguration::DocumentDisk& document, const std::vector<std::string>& categories, bool operationRunning)
	{
		ShaderConfiguration::PropertyDisk* selectedProperty = FindShaderConfigurationPropertyByID(document, gSelectedShaderConfigurationPropertyID);

		if (!selectedProperty)
		{
			gSelectedShaderConfigurationPropertyID.clear();
			gEditingShaderConfigurationPropertyID.clear();
		}

		std::string selectedPropertyHeader = "Selected Property###SelectedShaderConfigurationProperty";

		if (selectedProperty)
			selectedPropertyHeader = "Selected Property: " + selectedProperty->name + "###SelectedShaderConfigurationProperty";

		if (!ImGui::CollapsingHeader(selectedPropertyHeader.c_str()))
			return;

		if (!selectedProperty)
		{
			ImGui::TextDisabled("No property selected.");
			return;
		}

		ImGui::BeginDisabled(operationRunning);
		DrawShaderConfigurationCategorySelector(*selectedProperty, categories);

		//failed metadata writes can restore the document and replace its property storage.
		selectedProperty = FindShaderConfigurationPropertyByID(document, gSelectedShaderConfigurationPropertyID);

		if (selectedProperty->propertyType == ShaderConfiguration::PropertyType::Shader)
		{
			PropertyWidgets::ReadOnlyText("Shader property", "SelectedShaderPropertyName", selectedProperty->name);
			PropertyWidgets::ReadOnlyText("Description", "SelectedShaderPropertyDescription", selectedProperty->description);
			PropertyWidgets::ReadOnlyText("Value type", "SelectedShaderPropertyType", selectedProperty->valueType);
			PropertyWidgets::ReadOnlyText("Default value", "SelectedShaderPropertyDefault", selectedProperty->defaultValue);
			PropertyWidgets::ReadOnlyText("Value range", "SelectedShaderPropertyRange", selectedProperty->valueRange);
			PropertyWidgets::ReadOnlyText("Source Shader Path", "SelectedShaderPropertySourcePath", selectedProperty->sourcePath);
		}
		else if (selectedProperty->propertyType == ShaderConfiguration::PropertyType::RenderPass)
		{
			if (gEditingShaderConfigurationPropertyID != selectedProperty->id)
			{
				gEditingShaderConfigurationPropertyID = selectedProperty->id;
				std::snprintf(gSelectedPropertyName, sizeof(gSelectedPropertyName), "%s", selectedProperty->name.c_str());
				std::snprintf(gSelectedPropertyDescription, sizeof(gSelectedPropertyDescription), "%s", selectedProperty->description.c_str());
			}

			PropertyWidgets::PrepareRow("Name");
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##SelectedRenderPassPropertyName", gSelectedPropertyName, sizeof(gSelectedPropertyName));
			PropertyWidgets::PrepareRow("Description");
			ImGui::InputTextMultiline("##SelectedRenderPassPropertyDescription", gSelectedPropertyDescription, sizeof(gSelectedPropertyDescription), ImVec2(-FLT_MIN, 65.0f));
			std::vector<std::string> targetIDs = selectedProperty->renderPassIDs;
			if (targetIDs.empty() && !selectedProperty->renderPassID.empty())
				targetIDs.push_back(selectedProperty->renderPassID);
			std::string targetLabel;
			for (const std::string& targetID : targetIDs)
			{
				if (!targetLabel.empty())
					targetLabel += ", ";

				const auto renderPass = std::find_if(DatabaseRenderPasses::GetRenderPasses().begin(), DatabaseRenderPasses::GetRenderPasses().end(), [&targetID](const RenderPass::RenderPassDisk& candidate)
					{ return candidate.id == targetID; });
				if (renderPass != DatabaseRenderPasses::GetRenderPasses().end())
					targetLabel += renderPass->name + " (" + targetID + ")";
				else
					targetLabel += targetID;
			}
			PropertyWidgets::ReadOnlyText("Render pass binding", "SelectedRenderPassPropertyBinding", targetLabel);

			std::string propertyTypeName = "Render Pass Enable (Single)";
			if (selectedProperty->renderPassPropertyType == ShaderConfiguration::RenderPassPropertyType::EnabledMultiple)
				propertyTypeName = "Render Pass Enable (Multiple)";
			else if (selectedProperty->renderPassPropertyType == ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwo)
				propertyTypeName = "Render Pass Downscale Factor (Single)";
			else if (selectedProperty->renderPassPropertyType == ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwoMultiple)
				propertyTypeName = "Render Pass Downscale Factor (Multiple)";

			PropertyWidgets::ReadOnlyText("Property type", "SelectedRenderPassPropertyType", propertyTypeName);
			PropertyWidgets::ReadOnlyText("Default value", "SelectedRenderPassPropertyDefault", selectedProperty->defaultValue);

			if (ImGui::Button("Save property details##SelectedRenderPassProperty"))
			{
				const std::string originalName = selectedProperty->name;
				const std::string originalDescription = selectedProperty->description;
				selectedProperty->name = gSelectedPropertyName;
				selectedProperty->description = gSelectedPropertyDescription;
				std::string error;

				if (!DatabaseShaderConfigurations::SaveCatalog(error))
				{
					selectedProperty->name = originalName;
					selectedProperty->description = originalDescription;
					SetConfigurationManagementMessage(error);
				}
				else
				{
					if (selectedProperty->name != originalName || selectedProperty->description != originalDescription)
						MarkShaderConfigurationEdited(false, false);
					SetConfigurationManagementMessage("");
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Remove property##SelectedRenderPassProperty"))
			{
				std::string error;

				const std::string selectedPropertyID = selectedProperty->id;

				if (DatabaseShaderConfigurations::RemoveProperty(selectedPropertyID, error))
				{
					gSelectedShaderConfigurationPropertyID.clear();
					gEditingShaderConfigurationPropertyID.clear();
					MarkShaderConfigurationEdited();
					SetConfigurationManagementMessage("");
				}
				else
					SetConfigurationManagementMessage(error);
			}
		}
		else
		{
			PropertyWidgets::ReadOnlyText("Property type", "SelectedConfigurationPropertyType", "Constant buffer (reserved)");
			PropertyWidgets::ReadOnlyText("Property name", "SelectedConfigurationPropertyName", selectedProperty->name);
			PropertyWidgets::ReadOnlyText("Description", "SelectedConfigurationPropertyDescription", selectedProperty->description);
		}

		ImGui::EndDisabled();
	}

	void DrawShaderConfiguration()
	{
		DatabaseShaderConfigurations::EnsureLoaded();
		ProcessShaderConfigurationOperation();
		ShaderConfiguration::DocumentDisk& configurationDocument = DatabaseShaderConfigurations::GetEditableDocument();
		const std::vector<std::string> categories = ShaderConfigurationCategoryNames(configurationDocument);

		const std::string shaderConfigurationHeader = "Shader Configuration: " + GetShaderConfigurationPresetName(configurationDocument) + "###ShaderConfiguration";

		if (!ImGui::CollapsingHeader(shaderConfigurationHeader.c_str()))
		{
			DrawShaderConfigurationProgress();
			return;
		}

		ImGui::Indent(indentSpace);
		ImGui::Spacing();
		DrawShaderConfigurationManagement(configurationDocument, categories);
		ImGui::Spacing();
		const std::string propertiesTitle = "Properties: " + std::to_string(configurationDocument.properties.size());
		ImGui::SeparatorText(propertiesTitle.c_str());

		if (gShaderConfigurationDirty)
		{
			ImGui::TextDisabled("(unsaved changes)");
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##ShaderConfigurationSearch", "Search properties...", gShaderConfigurationSearch, sizeof(gShaderConfigurationSearch));
		DrawShaderConfigurationProgress();

		const std::string lowercaseSearch = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(gShaderConfigurationSearch));
		const bool operationRunning = ShaderConfigurationOperationIsRunning();
		size_t visiblePropertyCount = 0;

		if (ImGui::BeginChild("ShaderConfiguration##PropertyList", ImVec2(0, 260), ImGuiChildFlags_Borders))
		{
			size_t sourceGroupStart = 0;

			while (sourceGroupStart < categories.size())
			{
				size_t matchingPropertyCount = 0;
				size_t totalPropertyCount = 0;

				for (const ShaderConfiguration::PropertyDisk& property : configurationDocument.properties)
				{
					if (property.category == categories[sourceGroupStart])
					{
						++totalPropertyCount;
						if (ShaderConfigurationPropertyMatchesSearch(property, lowercaseSearch))
							++matchingPropertyCount;
					}
				}

				if (matchingPropertyCount > 0)
				{
					std::string groupLabel = categories[sourceGroupStart] + ": " + std::to_string(totalPropertyCount);

					if (!lowercaseSearch.empty() && matchingPropertyCount != totalPropertyCount)
					{
						groupLabel += " (" + std::to_string(matchingPropertyCount) + " matching)";
					}

					ImGui::PushID(categories[sourceGroupStart].c_str());

					if (!lowercaseSearch.empty())
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);

					const bool sourceGroupOpen = ImGui::TreeNodeEx("##ShaderConfigurationCategory", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel.c_str());

					if (sourceGroupOpen)
					{
						for (ShaderConfiguration::PropertyDisk& property : configurationDocument.properties)
						{
							if (property.category != categories[sourceGroupStart] || !ShaderConfigurationPropertyMatchesSearch(property, lowercaseSearch))
							{
								continue;
							}

							++visiblePropertyCount;

							bool propertyChanged = false;

							if (property.propertyType == ShaderConfiguration::PropertyType::Shader)
								propertyChanged = DrawShaderConfigurationProperty(property, operationRunning);
							else if (property.propertyType == ShaderConfiguration::PropertyType::RenderPass)
								propertyChanged = DrawRenderPassConfigurationProperty(property, operationRunning);
							else
							{
				ImGui::PushID(property.id.c_str());
								const std::string helpText = "Reserved for constant-buffer configuration.";

								if (PropertyWidgets::PrepareSelectableRow(property.name.c_str(), helpText.c_str(), gSelectedShaderConfigurationPropertyID == property.id))
									gSelectedShaderConfigurationPropertyID = property.id;
								ImGui::TextDisabled("Constant-buffer configuration is reserved.");
				ImGui::PopID();
							}

							if (propertyChanged)
							{
								MarkShaderConfigurationEdited();
							}

						}

						ImGui::TreePop();
					}
					else
					{
						visiblePropertyCount += matchingPropertyCount;
					}

					ImGui::PopID();
				}

				++sourceGroupStart;
			}

			if (configurationDocument.properties.empty())
			{
				ImGui::TextWrapped("No configurable #define properties were found in ModifiedShaders.");
			}
			else if (visiblePropertyCount == 0)
			{
				ImGui::TextUnformatted("No properties match the current search.");
			}
		}

		ImGui::EndChild();
		ImGui::BeginDisabled((configurationDocument.properties.empty() && !gShaderConfigurationNeedsApply) || ShaderConfigurationOperationIsRunning());

		if (ImGui::Button("Apply Property Changes##ShaderConfiguration"))
			ApplyShaderConfigurationPropertyChanges();

		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(ShaderConfigurationOperationIsRunning());

		if (ImGui::Button("Reload Properties##ShaderConfiguration"))
		{
			if (gShaderConfigurationDirty)
				SetConfigurationManagementMessage("Save the current preset before reloading properties.");
			else if (DatabaseShaderConfigurations::QueueReloadProperties())
				gShaderConfigurationFinishedTime = -1.0;
		}

		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(ShaderConfigurationOperationIsRunning());

		if (ImGui::Button("Force Recompile All##ShaderConfiguration"))
			ApplyShaderConfigurationPropertyChanges(true);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Apply current properties and recompile all shaders.");

		ImGui::EndDisabled();
		ImGui::Spacing();
		if (Globals::gShaderInjectorAdvancedMode)
		{
			DrawSelectedShaderConfigurationProperty(configurationDocument, categories, ShaderConfigurationOperationIsRunning());
			if (ImGui::CollapsingHeader("Configuration Setup###ShaderConfigurationSetup"))
			{
				const std::vector<std::string> currentCategories = ShaderConfigurationCategoryNames(configurationDocument);
				ImGui::SeparatorText("Categories");
				DrawShaderConfigurationCategories(currentCategories);
				const std::vector<std::string> updatedCategories = ShaderConfigurationCategoryNames(configurationDocument);
				ImGui::BeginDisabled(ShaderConfigurationOperationIsRunning());
				DrawAddRenderPassProperty(updatedCategories);
				ImGui::EndDisabled();
			}
		}

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
} //namespace ShaderInjectorGUI
