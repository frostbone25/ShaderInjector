#include "imgui.h"
#include "imgui_internal.h"

#include "GUI/ModifiedShaderBatchRecompileResult.h"
#include "GUI/PropertyWidgets.h"
#include "GUI/ShaderInjectorGUI.h"
#include "Globals.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "RenderPass/RenderPassConfigurationProperties.h"
#include "RenderPass/RenderPassShaderCompileQueue.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "ShaderConfiguration/ConfigurationProgress.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace ShaderInjectorGUI
{
	bool DrawShaderConfigurationProperty(ShaderConfiguration::PropertyDisk& property, bool operationRunning);
	void DrawSelectedShaderConfigurationProperty(ShaderConfiguration::DocumentDisk& document, const std::vector<std::string>& categories, bool operationRunning);
	bool SelectShaderConfigurationPreset(const std::string& presetID);
	bool ApplyShaderConfigurationPropertyChanges(bool forceRecompileAll = false);
	void MarkShaderConfigurationEdited(bool markDirty = true, bool needsApply = true);
}

namespace
{
	int failureCount = 0;
	int guiErrorCount = 0;
	int runtimeConfigurationInvalidationCount = 0;
	int runtimeConfigurationPublicationCount = 0;
	int modifiedShaderCompileQueueCount = 0;
	int renderPassShaderCompileQueueCount = 0;
	bool lastModifiedShaderIncludeInactive = false;
	bool lastModifiedShaderCompileForced = false;
	bool lastModifiedShaderHadPreviousConfiguration = false;
	bool lastRenderPassCompileForced = false;
	bool lastRenderPassHadPreviousConfiguration = false;
	int modifiedShaderCancellationCount = 0;
	int renderPassCancellationCount = 0;
	std::vector<RenderPass::RenderPassDisk> renderPasses;

	void Check(bool condition, const std::string& message)
	{
		if (condition)
			return;

		std::fprintf(stderr, "FAILED: %s\n", message.c_str());
		++failureCount;
	}

	void CaptureImGuiError(ImGuiContext*, void*, const char* message)
	{
		++guiErrorCount;
		std::fprintf(stderr, "ImGui recovery: %s\n", message);
	}

	void BeginFrame(float displayWidth, float displayHeight, const ImVec2& mousePosition = ImVec2(-100.0f, -100.0f), bool mouseDown = false)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(displayWidth, displayHeight);
		io.DeltaTime = 1.0f / 60.0f;
		io.AddMousePosEvent(mousePosition.x, mousePosition.y);
		io.AddMouseButtonEvent(0, mouseDown);
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(displayWidth, displayHeight), ImGuiCond_Always);
		ImGui::Begin("Shader Configuration UI Validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
	}

	void EndFrame()
	{
		ImGui::End();
		ImGui::Render();
	}

	int CountVerticesWithColor(const ImDrawList* drawList, int firstVertex, ImU32 color)
	{
		int matchingVertexCount = 0;
		for (int vertexIndex = firstVertex; vertexIndex < drawList->VtxBuffer.Size; ++vertexIndex)
		{
			if (drawList->VtxBuffer[vertexIndex].col == color)
				++matchingVertexCount;
		}

		return matchingVertexCount;
	}

	bool DrawSelectablePropertyRow(bool selected, const ImVec2& mousePosition, bool mouseDown, bool& outClicked, ImRect& outLabelRect, ImRect& outControlRect)
	{
		BeginFrame(840.0f, 420.0f, mousePosition, mouseDown);
		bool value = false;
		outClicked = PropertyWidgets::PrepareSelectableRow(
			"A wrapped property label that stays inside its label cell",
			"Selectable property help text",
			selected);
		outLabelRect = GImGui->LastItemData.Rect;
		Check(outLabelRect.GetHeight() > ImGui::GetFontSize(), "long selectable property label wraps within its cell");
		ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
		const ImGuiID expectedControlID = currentWindow->GetID("##PropertyValue");
		ImGui::Checkbox("##PropertyValue", &value);
		outControlRect = GImGui->LastItemData.Rect;
		Check(GImGui->LastItemData.ID == expectedControlID, "selectable row leaves the native value widget ID intact");
		Check(outControlRect.Min.x >= outLabelRect.Max.x, "value control begins beyond the selectable label cell");
		EndFrame();
		return true;
	}

	void CheckSelectablePropertyRow()
	{
		bool clicked = false;
		ImRect labelRect;
		ImRect controlRect;
		DrawSelectablePropertyRow(false, ImVec2(-100.0f, -100.0f), false, clicked, labelRect, controlRect);

		const ImVec2 clickPosition((labelRect.Min.x + labelRect.Max.x) * 0.5f, (labelRect.Min.y + labelRect.Max.y) * 0.5f);
		DrawSelectablePropertyRow(false, clickPosition, false, clicked, labelRect, controlRect);
		Check(!clicked, "hovering the label cell alone does not select the property");
		DrawSelectablePropertyRow(false, clickPosition, true, clicked, labelRect, controlRect);
		Check(!clicked, "selectable row waits for mouse release before reporting a click");
		DrawSelectablePropertyRow(false, clickPosition, false, clicked, labelRect, controlRect);
		Check(clicked, "clicking the full label cell selects the property on release");

		BeginFrame(840.0f, 420.0f);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const int firstVertex = drawList->VtxBuffer.Size;
		PropertyWidgets::PrepareSelectableRow("Selected property", nullptr, true);
		const ImU32 selectedColor = ImGui::GetColorU32(ImGuiCol_Header);
		Check(CountVerticesWithColor(drawList, firstVertex, selectedColor) >= 4, "selected property draws a visible header color highlight");
		float firstValue = 0.0f;
		ImGui::DragFloat("##FirstValue", &firstValue);
		const ImRect firstControlRect = GImGui->LastItemData.Rect;
		PropertyWidgets::PrepareSelectableRow("Second property with a wrapped label", nullptr, false);
		const ImRect secondLabelRect = GImGui->LastItemData.Rect;
		float secondValue = 0.0f;
		ImGui::DragFloat("##SecondValue", &secondValue);
		Check(secondLabelRect.Min.y >= firstControlRect.Min.y + firstControlRect.GetHeight(), "wrapped property rows do not overlap the previous value control");
		EndFrame();
	}

	void DrawShaderPropertyAndInspector(
		ShaderConfiguration::DocumentDisk& document,
		ShaderConfiguration::PropertyDisk& property,
		const std::vector<std::string>& categories)
	{
		ShaderInjectorGUI::DrawShaderConfigurationProperty(property, false);
		ShaderInjectorGUI::DrawSelectedShaderConfigurationProperty(document, categories, false);
	}

	void CheckConfigurationPropertySelection()
	{
		ShaderConfiguration::DocumentDisk& document = DatabaseShaderConfigurations::GetEditableDocument();
		ShaderConfiguration::PropertyDisk* property = nullptr;
		for (ShaderConfiguration::PropertyDisk& candidate : document.properties)
		{
			if (candidate.name == "CONFIGURED_VALUE" && candidate.propertyType == ShaderConfiguration::PropertyType::Shader)
			{
				property = &candidate;
				break;
			}
		}
		Check(property != nullptr, "find shader property for the selected-property inspector test");
		if (!property)
			return;

		const std::vector<std::string> categories = {"Uncategorized"};
		ImVec2 rowStart;
		float labelCellWidth = 0.0f;
		BeginFrame(1000.0f, 700.0f);
		rowStart = ImGui::GetCursorScreenPos();
		labelCellWidth = ImGui::GetContentRegionAvail().x / 3.0f;
		const float labelClickOffsetY = ImGui::GetStyle().FramePadding.y + ImGui::GetFrameHeight() * 0.5f;
		DrawShaderPropertyAndInspector(document, *property, categories);
		EndFrame();

		const ImVec2 labelClickPosition(
			rowStart.x + (std::min)(labelCellWidth * 0.5f, 80.0f),
			rowStart.y + labelClickOffsetY);
		BeginFrame(1000.0f, 700.0f, labelClickPosition, false);
		DrawShaderPropertyAndInspector(document, *property, categories);
		EndFrame();
		BeginFrame(1000.0f, 700.0f, labelClickPosition, true);
		DrawShaderPropertyAndInspector(document, *property, categories);
		EndFrame();

		BeginFrame(1000.0f, 700.0f, labelClickPosition, false);
		DrawShaderPropertyAndInspector(document, *property, categories);
		const ImGuiID inspectorHeaderID = ImGui::GetID("###SelectedShaderConfigurationProperty");
		Check(GImGui->LastItemData.ID == inspectorHeaderID, "selected-property inspector starts collapsed");
		EndFrame();

		BeginFrame(1000.0f, 700.0f);
		ImGui::GetStateStorage()->SetInt(inspectorHeaderID, 1);
		DrawShaderPropertyAndInspector(document, *property, categories);
		Check(GImGui->LastItemData.ID != 0 && GImGui->LastItemData.ID != inspectorHeaderID, "opening the selected property draws its real readonly inspector controls");
		EndFrame();

		ShaderConfiguration::DocumentDisk documentWithoutSelectedProperty = document;
		documentWithoutSelectedProperty.properties.erase(
			std::remove_if(
				documentWithoutSelectedProperty.properties.begin(),
				documentWithoutSelectedProperty.properties.end(),
				[&property](const ShaderConfiguration::PropertyDisk& candidate)
				{ return candidate.id == property->id; }),
			documentWithoutSelectedProperty.properties.end());
		BeginFrame(1000.0f, 700.0f);
		ShaderInjectorGUI::DrawSelectedShaderConfigurationProperty(documentWithoutSelectedProperty, categories, false);
		Check(GImGui->LastItemData.ID == 0, "inspector clears a selected ID missing from the current document");
		EndFrame();
	}

	float CheckPanelFrame(bool shouldBeOpen, const std::string& frameName, bool advancedMode = true)
	{
		const int errorsBeforeFrame = guiErrorCount;
		Globals::gShaderInjectorAdvancedMode = advancedMode;
		BeginFrame(1200.0f, 900.0f);
		//exercise the nested setup controls as well as the panel's outer disabled scope.
		ImGui::GetStateStorage()->SetInt(ImGui::GetID("Preset##ShaderConfiguration"), 1);
		ImGui::GetStateStorage()->SetInt(ImGui::GetID("Configuration Setup###ShaderConfigurationSetup"), 1);
		ImGui::SetNextItemOpen(shouldBeOpen, ImGuiCond_Always);
		const int disabledStackSize = GImGui->DisabledStackSize;
		const int itemFlagsStackSize = GImGui->ItemFlagsStack.Size;
		const int popupStackSize = GImGui->BeginPopupStack.Size;
		ShaderInjectorGUI::DrawShaderConfiguration();
		const float panelBottom = ImGui::GetCurrentWindow()->DC.CursorMaxPos.y;
		Check(GImGui->DisabledStackSize == disabledStackSize, frameName + " balances BeginDisabled/EndDisabled scopes");
		Check(GImGui->ItemFlagsStack.Size == itemFlagsStackSize, frameName + " balances item flag scopes");
		Check(GImGui->BeginPopupStack.Size == popupStackSize, frameName + " balances popup scopes");
		EndFrame();
		Check(guiErrorCount == errorsBeforeFrame, frameName + " does not trigger ImGui stack recovery errors");
		return panelBottom;
	}

	void CheckPresetSelectionAppliesValues()
	{
		const std::string originalPresetID = DatabaseShaderConfigurations::GetEditableDocument().presetID;
		std::string selectedPresetID;

		for (const ShaderConfiguration::PresetDisk& preset : DatabaseShaderConfigurations::GetPresets())
		{
			if (preset.id != originalPresetID)
			{
				selectedPresetID = preset.id;
				break;
			}
		}

		Check(!selectedPresetID.empty(), "find an alternate preset for automatic apply");

		if (selectedPresetID.empty())
			return;

		Check(ShaderInjectorGUI::SelectShaderConfigurationPreset(selectedPresetID), "preset selection queues property changes automatically");
		const ShaderConfiguration::DocumentDisk selectedDocument = DatabaseShaderConfigurations::GetEditableDocument();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

		while (DatabaseShaderConfigurations::GetAppliedDocument().presetID != selectedPresetID && std::chrono::steady_clock::now() < deadline)
		{
			ShaderInjectorGUI::ProcessShaderConfigurationOperation();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		const ShaderConfiguration::DocumentDisk appliedDocument = DatabaseShaderConfigurations::GetAppliedDocument();
		Check(appliedDocument.presetID == selectedPresetID, "preset selection publishes the selected preset without pressing Apply");

		for (const ShaderConfiguration::PropertyDisk& selectedProperty : selectedDocument.properties)
		{
			const auto appliedProperty = std::find_if(appliedDocument.properties.begin(), appliedDocument.properties.end(), [&selectedProperty](const ShaderConfiguration::PropertyDisk& property)
			{
				return property.id == selectedProperty.id;
			});

			Check(appliedProperty != appliedDocument.properties.end() && appliedProperty->value == selectedProperty.value, "automatic apply publishes the selected preset's property values");
		}
	}

	void CheckFrameworkPropertyApplyRefreshesShadersAndHistory()
	{
		ShaderConfiguration::DocumentDisk& editableDocument = DatabaseShaderConfigurations::GetEditableDocument();
		ShaderConfiguration::PropertyDisk downscaleProperty{};
		downscaleProperty.id = "ui-test-downscale";
		downscaleProperty.name = "UI Test Downscale";
		downscaleProperty.propertyType = ShaderConfiguration::PropertyType::RenderPass;
		downscaleProperty.renderPassID = "FrameworkApplyPass";
		downscaleProperty.renderPassPropertyType = ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwo;
		downscaleProperty.valueType = "int";
		downscaleProperty.defaultValue = "1";
		downscaleProperty.value = "2";
		editableDocument.properties.push_back(downscaleProperty);

		const auto appliedConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		std::string previousCompileFingerprint;

		if (appliedConfiguration)
			previousCompileFingerprint = appliedConfiguration->GetFingerprint();
		const int previousInvalidationCount = runtimeConfigurationInvalidationCount;
		const int previousPublicationCount = runtimeConfigurationPublicationCount;
		const int previousModifiedShaderQueueCount = modifiedShaderCompileQueueCount;
		const int previousRenderPassQueueCount = renderPassShaderCompileQueueCount;

		std::string validationError;
		const bool configurationIsValid = RenderPass::ValidateConfigurationProperties(editableDocument, renderPasses, validationError);
		Check(configurationIsValid, "framework-only configuration validates: " + validationError);
		Check(ShaderInjectorGUI::ApplyShaderConfigurationPropertyChanges(), "framework-only property change queues apply");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

		while (std::chrono::steady_clock::now() < deadline)
		{
			ShaderInjectorGUI::ProcessShaderConfigurationOperation();
			const ShaderConfiguration::DocumentDisk appliedDocument = DatabaseShaderConfigurations::GetAppliedDocument();
			const auto appliedProperty = std::find_if(appliedDocument.properties.begin(), appliedDocument.properties.end(), [](const ShaderConfiguration::PropertyDisk& property)
			{
				return property.id == "ui-test-downscale";
			});

			if (appliedProperty != appliedDocument.properties.end() && appliedProperty->value == "2")
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		ShaderInjectorGUI::ProcessShaderConfigurationOperation();
		Check(runtimeConfigurationInvalidationCount == previousInvalidationCount + 1, "framework-property apply invalidates render-pass history once");
		Check(runtimeConfigurationPublicationCount == previousPublicationCount, "framework-property apply uses the invalidating publication path");
		Check(lastModifiedShaderHadPreviousConfiguration, "framework-property apply passes the previous configuration to modified shader selection");
		Check(lastRenderPassHadPreviousConfiguration, "framework-property apply passes the previous configuration to render-pass selection");

		const auto currentConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		std::string currentCompileFingerprint;

		if (currentConfiguration)
			currentCompileFingerprint = currentConfiguration->GetFingerprint();
		Check(currentCompileFingerprint == previousCompileFingerprint, "framework-only change does not alter the shader compile fingerprint");

		const int forcePreviousModifiedShaderQueueCount = modifiedShaderCompileQueueCount;
		const int forcePreviousRenderPassQueueCount = renderPassShaderCompileQueueCount;
		Check(ShaderInjectorGUI::ApplyShaderConfigurationPropertyChanges(true), "force recompile queues property apply");
		const auto forceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

		while (std::chrono::steady_clock::now() < forceDeadline)
		{
			ShaderInjectorGUI::ProcessShaderConfigurationOperation();
			if (DatabaseShaderConfigurations::GetOperationProgressTracker()->GetSnapshot().finished)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		Check(modifiedShaderCompileQueueCount == forcePreviousModifiedShaderQueueCount + 1, "force recompile includes inactive modified shader packages");
		Check(lastModifiedShaderIncludeInactive, "force recompile passes inactive package inclusion");
		Check(lastModifiedShaderCompileForced, "force recompile passes a null previous configuration to modified shader compiles");
		Check(renderPassShaderCompileQueueCount == forcePreviousRenderPassQueueCount + 1, "force recompile queues every render-pass shader");
		Check(lastRenderPassCompileForced, "force recompile passes a null previous configuration");
	}

	void CheckPanelIdleAndBusyStates()
	{
		CheckPanelFrame(true, "open idle configuration panel");
		const float basicPanelBottom = CheckPanelFrame(true, "open basic configuration panel", false);
		const float advancedPanelBottom = CheckPanelFrame(true, "open advanced configuration panel");
		Check(basicPanelBottom < advancedPanelBottom, "basic mode hides the selected property and configuration setup sections");

		Check(DatabaseShaderConfigurations::QueueApplyChanges(), "queue a configuration apply for the busy panel frame");
		DatabaseShaderConfigurations::ConfigurationOperationResult operationResult;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < deadline)
		{
			operationResult = DatabaseShaderConfigurations::ProcessCompletedOperation();
			if (operationResult.completed)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		Check(operationResult.completed && operationResult.succeeded, "configuration preparation succeeds for the busy panel frame");

		const std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress = DatabaseShaderConfigurations::GetOperationProgressTracker();
		Check(progress != nullptr, "configuration operation exposes its progress tracker");
		Check(progress && progress->GetSnapshot().blocksEditing, "configuration preparation blocks edits until compile dispatch begins");
		if (progress)
		{
			progress->BeginCompiling();
			auto pendingTask = progress->BeginTask();
			auto priorityTask = progress->BeginTask(true);
			Check(progress->GetSnapshot().blocksEditing, "open compile dispatch keeps edits blocked before priority tasks finish");
			progress->FinishDispatch();
			priorityTask->MarkSucceeded();
			priorityTask.reset();

			const auto prioritySnapshot = progress->GetSnapshot();
			Check(prioritySnapshot.priorityTotal == 1 && prioritySnapshot.priorityCompleted == 1, "priority progress records completed active shader work");
			Check(prioritySnapshot.completed < prioritySnapshot.total, "priority completion leaves background shader work pending");
			Check(!prioritySnapshot.blocksEditing && !prioritySnapshot.finished, "active shader completion unlocks edits while background work remains");

			Globals::gShaderInjectorAdvancedMode = true;
			BeginFrame(1200.0f, 900.0f);
			ImGui::GetStateStorage()->SetInt(ImGui::GetID("Preset##ShaderConfiguration"), 1);
			ImGui::GetStateStorage()->SetInt(ImGui::GetID("Configuration Setup###ShaderConfigurationSetup"), 1);
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const int firstVertex = drawList->VtxBuffer.Size;
			ShaderInjectorGUI::DrawShaderConfiguration();
			const ImU32 priorityReadyColor = ImGui::GetColorU32(ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
			Check(CountVerticesWithColor(drawList, firstVertex, priorityReadyColor) >= 4, "priority progress bar turns green while background work remains");
			Check((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0, "configuration controls re-enable after active shader work completes");
			EndFrame();

			auto failedActiveTask = progress->BeginTask(true);
			failedActiveTask->MarkFailed();
			failedActiveTask.reset();
			const auto failedActiveSnapshot = progress->GetSnapshot();
			Check(failedActiveSnapshot.priorityFailed == 1 && !failedActiveSnapshot.blocksEditing, "failed active work unlocks controls for retry");

			BeginFrame(1200.0f, 900.0f);
			ImGui::GetStateStorage()->SetInt(ImGui::GetID("Preset##ShaderConfiguration"), 1);
			ImGui::GetStateStorage()->SetInt(ImGui::GetID("Configuration Setup###ShaderConfigurationSetup"), 1);
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
			drawList = ImGui::GetWindowDrawList();
			const int failedPriorityFirstVertex = drawList->VtxBuffer.Size;
			ShaderInjectorGUI::DrawShaderConfiguration();
			Check(CountVerticesWithColor(drawList, failedPriorityFirstVertex, priorityReadyColor) == 0, "failed active work does not turn the progress bar green");
			Check((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0, "configuration controls remain enabled after active compile failure");
			EndFrame();

			ShaderInjectorGUI::MarkShaderConfigurationEdited();
			Check(modifiedShaderCancellationCount == 1 && renderPassCancellationCount == 1, "first edit cancels the prior background compile batch once");
			Check(progress->GetSnapshot().finished && progress->GetSnapshot().cancellationRequested, "editing marks the old configuration compile batch canceled");
			ShaderInjectorGUI::MarkShaderConfigurationEdited();
			Check(modifiedShaderCancellationCount == 1 && renderPassCancellationCount == 1, "additional edits do not cancel the same background batch again");

			Check(ShaderInjectorGUI::ApplyShaderConfigurationPropertyChanges(), "explicit apply can start after background compilation is canceled");
			const auto applyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

			while (std::chrono::steady_clock::now() < applyDeadline)
			{
				ShaderInjectorGUI::ProcessShaderConfigurationOperation();
				const auto nextProgress = DatabaseShaderConfigurations::GetOperationProgressTracker();

				if (nextProgress && nextProgress != progress && nextProgress->GetSnapshot().finished)
					break;

				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			pendingTask.reset();
		}

		CheckPanelFrame(false, "collapsed configuration panel");

	}
}

bool RunShaderConfigurationUITests()
{
	failureCount = 0;
	guiErrorCount = 0;
	runtimeConfigurationInvalidationCount = 0;
	runtimeConfigurationPublicationCount = 0;
	modifiedShaderCompileQueueCount = 0;
	renderPassShaderCompileQueueCount = 0;
	lastModifiedShaderIncludeInactive = false;
	lastModifiedShaderCompileForced = false;
	lastModifiedShaderHadPreviousConfiguration = false;
	lastRenderPassCompileForced = false;
	lastRenderPassHadPreviousConfiguration = false;
	modifiedShaderCancellationCount = 0;
	renderPassCancellationCount = 0;
	renderPasses.clear();
	RenderPass::RenderPassDisk renderPass{};
	renderPass.id = "ExpansionTestPass";
	renderPass.name = "Expansion Test Pass";
	renderPasses.push_back(renderPass);
	renderPass.id = "ExpansionSecondPass";
	renderPass.name = "Expansion Second Pass";
	renderPasses.push_back(renderPass);
	renderPass.id = "ExpansionThirdPass";
	renderPass.name = "Expansion Third Pass";
	renderPasses.push_back(renderPass);
	renderPass.id = "OverlayPass";
	renderPass.name = "Overlay Pass";
	renderPasses.push_back(renderPass);
	renderPass.id = "ChainPass";
	renderPass.name = "Chain Pass";
	renderPass.type = RenderPass::RenderPassType::DownsampleUpsample;
	renderPasses.push_back(renderPass);
	renderPass.id = "FrameworkApplyPass";
	renderPass.name = "Framework Apply Pass";
	renderPass.type = RenderPass::RenderPassType::Custom;
	renderPasses.push_back(renderPass);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigErrorRecoveryEnableAssert = false;
	io.ConfigErrorRecoveryEnableDebugLog = false;
	io.ConfigErrorRecoveryEnableTooltip = false;
	io.ConfigDebugHighlightIdConflicts = false;
	unsigned char* fontPixels = nullptr;
	int fontWidth = 0;
	int fontHeight = 0;
	io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
	GImGui->ErrorCallback = CaptureImGuiError;

	CheckSelectablePropertyRow();
	CheckConfigurationPropertySelection();
	CheckPanelIdleAndBusyStates();
	CheckFrameworkPropertyApplyRefreshesShadersAndHistory();
	CheckPresetSelectionAppliesValues();

	ImGui::DestroyContext();
	return failureCount == 0 && guiErrorCount == 0;
}

namespace ShaderInjectorGUI
{
	ModifiedShaderBatchRecompileResult RecompileModifiedShaders(
		bool includeInactivePackages,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration,
		bool)
	{
		++modifiedShaderCompileQueueCount;
		lastModifiedShaderIncludeInactive = includeInactivePackages;
		lastModifiedShaderCompileForced = !previousConfiguration;
		lastModifiedShaderHadPreviousConfiguration = static_cast<bool>(previousConfiguration);
		return {};
	}

	void WriteToRuntimeLogSuccess(std::string)
	{
	}
}

namespace DatabaseRenderPasses
{
	void CancelConfigurationCompiles(std::shared_ptr<ShaderConfiguration::ConfigurationProgress>)
	{
		++renderPassCancellationCount;
	}

	const std::vector<RenderPass::RenderPassDisk>& GetRenderPasses()
	{
		return renderPasses;
	}

	const RenderPass::RenderPassDisk* FindRenderPassByIdReadOnly(const std::string& renderPassID)
	{
		for (const RenderPass::RenderPassDisk& renderPass : renderPasses)
		{
			if (renderPass.id == renderPassID)
				return &renderPass;
		}

		return nullptr;
	}

	void PublishRuntimeConfiguration(bool invalidateHistory)
	{
		if (invalidateHistory)
			++runtimeConfigurationInvalidationCount;
		else
			++runtimeConfigurationPublicationCount;
	}

	RenderPassShaderBatchCompileResult QueueAllRenderPassShaderCompiles(std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration)
	{
		++renderPassShaderCompileQueueCount;
		lastRenderPassCompileForced = !previousConfiguration;
		lastRenderPassHadPreviousConfiguration = static_cast<bool>(previousConfiguration);
		return {};
	}
}

namespace DatabaseModifiedShaders
{
	void CancelConfigurationCompiles(std::shared_ptr<ShaderConfiguration::ConfigurationProgress>)
	{
		++modifiedShaderCancellationCount;
	}
}
