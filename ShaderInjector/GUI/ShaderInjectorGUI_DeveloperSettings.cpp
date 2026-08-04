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
	static int gSelectionStyleIndex = (int)HookD3D12::PixelShaderSelectionStyle::BluePixelShader;

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
			const RenderDocIntegration::CaptureRequestResult result = RenderDocIntegration::RequestFrameCapture(nullptr, Globals::mainWindow);

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
	template<typename PipelineT, uint64_t PipelineT::* HashMember, SIZE_T PipelineT::* SizeMember, std::vector<uint8_t> PipelineT::* BytecodeMember>
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
}
