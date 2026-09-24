//ShaderInjectorGUI.cpp
#include "ShaderInjectorGUI.h"
#include "GUI/ShaderModelOption.h"

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
#include "ShaderAutomaticDiscovery.h"
#include "StringHelper.h"
#include "GUI/ShaderInjectorGUITooltips.h"
#include "Keycodes.h"
#include "ShaderInjectorVersion.h"
#include "ShaderInjectorInternalResources.h"
#include "ShaderModelDetector.h"
#include "Enum/RenderDocCaptureRequestResult.h"
#include "Enum/RenderDocReplayUIRequestResult.h"

namespace
{
	using ShaderInjectorGUI::ShaderModelOption;

	constexpr ShaderModelOption shaderModelOptions[] =
	{
		{Globals::ShaderModel::ShaderModel5_0, "Shader Model 5.0 (DXBC)"},
		{Globals::ShaderModel::ShaderModel5_1, "Shader Model 5.1 (DXBC)"},
		{Globals::ShaderModel::ShaderModel6_0, "Shader Model 6.0 (DXIL)"},
		{Globals::ShaderModel::ShaderModel6_1, "Shader Model 6.1 (DXIL)"},
		{Globals::ShaderModel::ShaderModel6_2, "Shader Model 6.2 (DXIL)"},
		{Globals::ShaderModel::ShaderModel6_3, "Shader Model 6.3 (DXIL)"},
		{Globals::ShaderModel::ShaderModel6_4, "Shader Model 6.4 (DXIL)"},
		{Globals::ShaderModel::ShaderModel6_5, "Shader Model 6.5 (DXIL)"},
		{Globals::ShaderModel::ShaderModel6_6, "Shader Model 6.6 (DXIL)"},
	};

	bool DrawShaderModelCombo(const char* label, Globals::ShaderModel& shaderModel)
	{
		const char* previewLabel = shaderModelOptions[8].displayLabel;

		for (const ShaderModelOption& option : shaderModelOptions)
		{
			if (option.shaderModel == shaderModel)
			{
				previewLabel = option.displayLabel;
				break;
			}
		}

		bool changed = false;

		ImGui::SetNextItemWidth(220.0f * Globals::gShaderInjectorGUIScale);

		if (ImGui::BeginCombo(label, previewLabel))
		{
			for (const ShaderModelOption& option : shaderModelOptions)
			{
				const bool selected = shaderModel == option.shaderModel;

				if (ImGui::Selectable(option.displayLabel, selected))
				{
					shaderModel = option.shaderModel;
					changed = true;
				}

				if (selected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		return changed;
	}

	void DrawShaderModelSetting(
		const char* label,
		ShaderTarget::ShaderType shaderType,
		Globals::ShaderModel& configuredShaderModel)
	{
		Globals::ShaderModel displayedShaderModel = ShaderModelDetector::GetEffectiveShaderModel(shaderType, configuredShaderModel);
		Globals::ShaderModel detectedShaderModel = configuredShaderModel;
		const bool modelDetected = ShaderModelDetector::TryGetDetectedShaderModel(shaderType, detectedShaderModel);

		ImGui::BeginDisabled(Globals::gAutoDetectShaderModels);

		if (DrawShaderModelCombo(label, displayedShaderModel) && !Globals::gAutoDetectShaderModels)
			configuredShaderModel = displayedShaderModel;

		ImGui::EndDisabled();

		if (Globals::gAutoDetectShaderModels)
		{
			ImGui::SameLine();
			const char* detectionStatus = "fallback";
			if (modelDetected)
				detectionStatus = "detected";
			ImGui::TextDisabled("(%s)", detectionStatus);
		}
	}
} //namespace

namespace ShaderInjectorGUI
{
	static int gSelectionStyleIndex = (int)HookD3D12::PixelShaderSelectionStyle::BluePixelShader;

	void DrawRenderDoc()
	{
		const bool renderDocAvailable = RenderDocIntegration::IsAvailable();
		const bool frameCaptureActive = RenderDocIntegration::IsFrameCapturing();
		const bool targetControlConnected = RenderDocIntegration::IsTargetControlConnected();

		ImGui::SeparatorText("RenderDoc");
		ImGui::Text("Status: %s", RenderDocIntegration::GetStatusText().c_str());
		ImGui::Text("API Version: %s", RenderDocIntegration::GetApiVersionText().c_str());

		if (renderDocAvailable)
		{
			const char* librarySource = "External";

			if (RenderDocIntegration::WasLoadedByInjector())
				librarySource = "Shader Injector";

			ImGui::Text("Library Load: %s", librarySource);
		}

		const char* targetControlStatus = "Disconnected";
		const char* frameCaptureStatus = "Idle";

		if (targetControlConnected)
			targetControlStatus = "Connected";

		if (frameCaptureActive)
			frameCaptureStatus = "Active";

		ImGui::Text("Target Control: %s", targetControlStatus);
		ImGui::Text("Frame Capture: %s", frameCaptureStatus);
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
			const char* connectButtonLabel = "Attach RenderDoc";

			if (renderDocAvailable)
				connectButtonLabel = "Connect RenderDoc UI";

			if (ImGui::Button(connectButtonLabel))
			{
				const RenderDocReplayUIRequestResult result = RenderDocIntegration::ConnectReplayUI();

				switch (result)
				{
					case RenderDocReplayUIRequestResult::Launched:
						WriteToRuntimeLogSuccess("RenderDoc Replay UI launched and requested target control connection.");
						break;
					case RenderDocReplayUIRequestResult::AlreadyConnected:
						WriteToRuntimeLogSuccess("RenderDoc target control is already connected.");
						break;
					case RenderDocReplayUIRequestResult::Disabled:
						WriteToRuntimeLogWarning("RenderDoc integration is disabled in ShaderInjector.ini.");
						break;
					case RenderDocReplayUIRequestResult::Unavailable:
						WriteToRuntimeLogError("RenderDoc installation could not be found or loaded.");
						break;
					case RenderDocReplayUIRequestResult::LaunchFailed:
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
			const RenderDocCaptureRequestResult result = RenderDocIntegration::RequestFrameCapture(nullptr, Globals::mainWindow);

			switch (result)
			{
				case RenderDocCaptureRequestResult::Queued:
					WriteToRuntimeLogSuccess("RenderDoc frame capture queued for the next Present.");
					break;
				case RenderDocCaptureRequestResult::AlreadyCapturing:
					WriteToRuntimeLogWarning("RenderDoc is already capturing a frame.");
					break;
				case RenderDocCaptureRequestResult::Disabled:
					WriteToRuntimeLogWarning("RenderDoc integration is disabled in ShaderInjector.ini.");
					break;
				case RenderDocCaptureRequestResult::TargetUnavailable:
					WriteToRuntimeLogError("The game D3D12 device or window is not ready for capture.");
					break;
				case RenderDocCaptureRequestResult::Unavailable:
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

	void DrawShaderCompilerSettings()
	{
		ImGui::Checkbox("Automatically Detect Shader Models", &Globals::gAutoDetectShaderModels);
		DrawShaderModelSetting("Vertex Shader", ShaderTarget::VertexShader, Globals::gVertexShaderModel);
		DrawShaderModelSetting("Hull Shader", ShaderTarget::HullShader, Globals::gHullShaderModel);
		DrawShaderModelSetting("Domain Shader", ShaderTarget::DomainShader, Globals::gDomainShaderModel);
		DrawShaderModelSetting("Geometry Shader", ShaderTarget::GeometryShader, Globals::gGeometryShaderModel);
		DrawShaderModelSetting("Pixel Shader", ShaderTarget::PixelShader, Globals::gPixelShaderModel);
		DrawShaderModelSetting("Compute Shader", ShaderTarget::ComputeShader, Globals::gComputeShaderModel);

		if (ImGui::Button("Apply Shader Levels"))
		{
			if (ShaderInjectorInternalResources::RecompileAndReload())
			{
				HookD3D12::InvalidateShaderMarkerPSOs();
				WriteToRuntimeLogSuccess("Applied shader levels and reloaded internal marker shaders.");
			}
			else
			{
				WriteToRuntimeLogError("Could not compile and reload the internal marker shaders.");
			}
		}
	}

	void DrawDeveloperSettings()
	{
		if (ImGui::CollapsingHeader("Developer Settings"))
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			if (ImGui::BeginTabBar("##DeveloperSettingsTabs"))
			{
				if (ImGui::BeginTabItem("Shader Compiler"))
				{
					HookD3D12::ClearShaderMarkers();
					DrawShaderCompilerSettings();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Shader Inspector"))
				{
					ImGui::InputTextMultiline("##DeveloperSettingsNote",
											  const_cast<char*>(noteDeveloperSettingsText),
											  strlen(noteDeveloperSettingsText) + 1,
											  ImVec2(-FLT_MIN, 0), //-FLT_MIN width = stretch to window edge, 0 height = auto
											  ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

					ImGui::Spacing();
					DrawAdapterInfo();
					DrawD3D12PipelineInfo();
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

					DrawStreamPipelines();

					//show stream pipelines here because they contain the shader stages users edit in this game.
					ImGui::EndTabItem();
				}

				if (Globals::gRenderDocIntegrationEnabled && ImGui::BeginTabItem("RenderDoc"))
				{
					HookD3D12::ClearShaderMarkers();
					DrawRenderDoc();
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

	void DrawAdapterInfo()
	{
		if (ImGui::TreeNodeEx("Adapter Info"))
		{
			ImGui::SeparatorText("GPU");
			ImGui::Text("Adapter: %s", HookD3D12::gPipelineInfo.graphicsProcessorName.c_str());
			ImGui::Text("Vendor ID: 0x%X", HookD3D12::gPipelineInfo.vendorID);
			ImGui::Text("Device ID: 0x%X", HookD3D12::gPipelineInfo.deviceID);
			ImGui::SeparatorText("Memory");
			ImGui::Text("Dedicated VRAM: %.2f GB", HookD3D12::gPipelineInfo.dedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0));
			ImGui::Text("Dedicated System: %.2f GB", HookD3D12::gPipelineInfo.dedicatedSystemMemory / (1024.0 * 1024.0 * 1024.0));
			ImGui::Text("Shared System: %.2f GB", HookD3D12::gPipelineInfo.sharedSystemMemory / (1024.0 * 1024.0 * 1024.0));
			ImGui::Separator();
			ImGui::TreePop();
		}
	}

	void DrawD3D12PipelineInfo()
	{
		if (ImGui::TreeNodeEx("D3D12 Pipeline Info"))
		{
			ImGui::SeparatorText("Swap Chain");
			ImGui::Text("Buffers: %u", HookD3D12::gPipelineInfo.swapChainBufferCount);
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

	void DrawStreamPipelines()
	{
		std::string headerText = "Stream Pipelines: " + std::to_string(HookD3D12::gPipelineStates.size()) + " PSOs";

		if (ImGui::CollapsingHeader(headerText.c_str()))
		{
			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::pixelShaderHash, &HookD3D12::PipelineStateInfo::pixelShaderBytecodeSize, &HookD3D12::PipelineStateInfo::pixelShaderBytecode>(
				"Pixel Shaders", "StreamPS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::PixelShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, HookD3D12::PipelineSourceList::Stream, true, false, &HookD3D12::PipelineStateInfo::pixelShaderDisabled, &HookD3D12::PipelineStateInfo::pipelineStateWithoutPixelShader);

			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::computeShaderHash, &HookD3D12::PipelineStateInfo::computeShaderBytecodeSize, &HookD3D12::PipelineStateInfo::computeShaderBytecode>(
				"Compute Shaders", "StreamCS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::ComputeShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, HookD3D12::PipelineSourceList::Stream, true, false, &HookD3D12::PipelineStateInfo::computeShaderDisabled, &HookD3D12::PipelineStateInfo::pipelineStateWithoutComputeShader);

			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::vertexShaderHash, &HookD3D12::PipelineStateInfo::vertexShaderBytecodeSize, &HookD3D12::PipelineStateInfo::vertexShaderBytecode>(
				"Vertex Shaders", "StreamVS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::VertexShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, HookD3D12::PipelineSourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::vertexShaderDisabled, &HookD3D12::PipelineStateInfo::pipelineStateWithoutVertexShader);

			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::geometryShaderHash, &HookD3D12::PipelineStateInfo::geometryShaderBytecodeSize, &HookD3D12::PipelineStateInfo::geometryShaderBytecode>(
				"Geometry Shaders", "StreamGS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::GeometryShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, HookD3D12::PipelineSourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::geometryShaderDisabled, &HookD3D12::PipelineStateInfo::pipelineStateWithoutGeometryShader);

			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::hullShaderHash, &HookD3D12::PipelineStateInfo::hullShaderBytecodeSize, &HookD3D12::PipelineStateInfo::hullShaderBytecode>(
				"Hull Shaders", "StreamHS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::HullShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, HookD3D12::PipelineSourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::hullShaderDisabled, &HookD3D12::PipelineStateInfo::pipelineStateWithoutHullShader);

			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::domainShaderHash, &HookD3D12::PipelineStateInfo::domainShaderBytecodeSize, &HookD3D12::PipelineStateInfo::domainShaderBytecode>(
				"Domain Shaders", "StreamDS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::DomainShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, HookD3D12::PipelineSourceList::Stream, true, true, &HookD3D12::PipelineStateInfo::domainShaderDisabled, &HookD3D12::PipelineStateInfo::pipelineStateWithoutDomainShader);
		}
		else
		{
			HookD3D12::ClearShaderMarkers();
		}
	}

	//UI "Template" for each of the shader stages
	template <typename PipelineT, uint64_t PipelineT::*HashMember, SIZE_T PipelineT::*SizeMember, std::vector<uint8_t> PipelineT::*BytecodeMember>
	void DrawShaderStageList(
		const char* stageLabel,
		const char* idPrefix,
		const char* sourceList,
		std::vector<PipelineT>& pipelines,
		ShaderTarget::ShaderType shaderType,
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE subobjectType,
		HookD3D12::PipelineSourceList pendingSource,
		bool allowMarkerToggle,
		bool disableActions,
		bool PipelineT::*disabledMember,
		ID3D12PipelineState* PipelineT::*rebuiltPSOMember)
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
			else if (sortMode == 2)
			{
				// The vector position is the pipeline index used by the capture and
				// rebuild systems, so preserve its natural ascending order here.
				if (a != b)
					return a < b;
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

		ImGui::SameLine();

		std::string pipelineIndexSortLabel = std::string("Pipeline Index##") + idPrefix;
		ImGui::RadioButton(pipelineIndexSortLabel.c_str(), &sortMode, 2);

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
							HookD3D12::gPendingRebuilds.push_back({pendingSource, selectedIndex, subobjectType});
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
		const char* selectedReplacementName = "(none)";

		if (selectedReplacementIndex >= 0)
			selectedReplacementName = HookD3D12::gLoadedShaderTargets[selectedReplacementIndex].name.c_str();

		ImGui::SeparatorText("Selected Shader");
		ImGui::Text("Pipeline Index: %d", selectedIndex);
		ImGui::Text("Hash: %s", Hash::FormatHash(hash).c_str());
		ImGui::Text("Bytecode Length: %zu", (size_t)bytecodeSize);
		ImGui::Text("Shader Replacement: %s", selectedReplacementName);
		ImGui::Text("PSO: %p", pipeline.pipelineState);

		if (allowMarkerToggle && disabledMember && rebuiltPSOMember)
		{
			const char* markerStatus = "inactive";

			if (pipeline.*disabledMember)
				markerStatus = "active";

			ImGui::Text("Marker: %s", markerStatus);

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

				if (ModifiedShaderCreation::CreateTemplate(shaderType, hash, bytecode.data(), bytecode.size(), creationMessage))
				{
					WriteToRuntimeLogSuccess(creationMessage);

					if (!ShaderAutomaticDiscovery::ProcessCapturedShader(pipeline, selectedIndex, shaderType, hash, bytecode))
					{
						WriteToRuntimeLogError("Modified Shader was created, but automatic Shader Target creation failed.");
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

	template <typename PipelineT, uint64_t PipelineT::*HashMember>
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

	template <typename PipelineT, uint64_t PipelineT::*HashMember>
	int FindFirstShaderStageIndex(const std::vector<PipelineT>& pipelines)
	{
		for (int i = 0; i < (int)pipelines.size(); i++)
		{
			if (pipelines[i].*HashMember)
				return i;
		}

		return -1;
	}
} //namespace ShaderInjectorGUI
