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
#include "HookD3D12/HookD3D12RetiredPSOTracker.h"
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

	bool DrawShaderModelCombo(const char* label, const char* controlID, Globals::ShaderModel& shaderModel, Globals::ShaderModel minimumShaderModel)
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

		if (PropertyWidgets::BeginCombo(label, controlID, previewLabel))
		{
			for (const ShaderModelOption& option : shaderModelOptions)
			{
				if (option.shaderModel < minimumShaderModel)
					continue;

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
		Globals::ShaderModel minimumShaderModel = Globals::ShaderModel::ShaderModel5_0;
		if (shaderType == ShaderTarget::AmplificationShader || shaderType == ShaderTarget::MeshShader)
		{
			minimumShaderModel = Globals::ShaderModel::ShaderModel6_5;
			if (displayedShaderModel < minimumShaderModel)
				displayedShaderModel = minimumShaderModel;
		}

		std::string propertyLabel = label;

		if (Globals::gAutoDetectShaderModels)
		{
			const char* detectionStatus = "fallback";
			if (modelDetected)
				detectionStatus = "detected";
			propertyLabel += ": " + std::string(detectionStatus);
		}

		ImGui::BeginDisabled(Globals::gAutoDetectShaderModels);

		//keep the control ID stable when the visible detection status changes.
		if (DrawShaderModelCombo(propertyLabel.c_str(), label, displayedShaderModel, minimumShaderModel) && !Globals::gAutoDetectShaderModels)
			configuredShaderModel = displayedShaderModel;

		ImGui::EndDisabled();
	}
} //namespace

namespace ShaderInjectorGUI
{
	static int gSelectionStyleIndex = (int)HookD3D12::PixelShaderSelectionStyle::BluePixelShader;

	void DrawRenderDoc()
	{
		//keep the header visible but unavailable until RenderDoc is enabled in the INI.
		ImGui::BeginDisabled(!Globals::gRenderDocIntegrationEnabled);
		const bool renderDocOpen = ImGui::CollapsingHeader("RenderDoc");
		ImGui::EndDisabled();

		if (!Globals::gRenderDocIntegrationEnabled || !renderDocOpen)
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		const bool renderDocAvailable = RenderDocIntegration::IsAvailable();
		const bool frameCaptureActive = RenderDocIntegration::IsFrameCapturing();
		const bool targetControlConnected = RenderDocIntegration::IsTargetControlConnected();

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

		ImGui::Unindent(indentSpace);
	}

	void DrawShaderCompilerSettings()
	{
		if (!ImGui::CollapsingHeader("Shader Compiler"))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		PropertyWidgets::Checkbox("Automatically Detect Shader Models", "Automatically Detect Shader Models", &Globals::gAutoDetectShaderModels);
		DrawShaderModelSetting("Vertex Shader", ShaderTarget::VertexShader, Globals::gVertexShaderModel);
		DrawShaderModelSetting("Hull Shader", ShaderTarget::HullShader, Globals::gHullShaderModel);
		DrawShaderModelSetting("Domain Shader", ShaderTarget::DomainShader, Globals::gDomainShaderModel);
		DrawShaderModelSetting("Geometry Shader", ShaderTarget::GeometryShader, Globals::gGeometryShaderModel);
		DrawShaderModelSetting("Pixel Shader", ShaderTarget::PixelShader, Globals::gPixelShaderModel);
		DrawShaderModelSetting("Compute Shader", ShaderTarget::ComputeShader, Globals::gComputeShaderModel);
		DrawShaderModelSetting("Amplification Shader", ShaderTarget::AmplificationShader, Globals::gAmplificationShaderModel);
		DrawShaderModelSetting("Mesh Shader", ShaderTarget::MeshShader, Globals::gMeshShaderModel);

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

		ImGui::Unindent(indentSpace);
	}

	static void DrawRetiredPSODiagnostics()
	{
		const HookD3D12::RetiredPSODiagnostics diagnostics = HookD3D12::GetRetiredPSODiagnostics();

		if (!ImGui::TreeNodeEx("Retired PSO Tracker", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		PropertyWidgets::ReadOnlyText("Pending", "##RetiredPSOPending", std::to_string(diagnostics.pendingCount));
		PropertyWidgets::ReadOnlyText("Retired total", "##RetiredPSORetiredTotal", std::to_string(diagnostics.retiredTotal));
		PropertyWidgets::ReadOnlyText("Released total", "##RetiredPSOReleasedTotal", std::to_string(diagnostics.releasedTotal));
		PropertyWidgets::ReadOnlyText("Oldest pending age", "##RetiredPSOOldestPendingAge", StringHelper::Format("%llu ms", static_cast<unsigned long long>(diagnostics.oldestPendingAgeMilliseconds)));

		if (diagnostics.pendingCount == 0)
			ImGui::TextDisabled("No pending references; every retired PSO has released.");
		else if (diagnostics.oldestPendingAgeMilliseconds > 60000)
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Oldest pending older than 60s; a submit never re-fenced after its last observation.");
		else
			ImGui::TextDisabled("Pending references wait for fence-proof of the next submit.");

		ImGui::TreePop();
	}

	void DrawShaderInspector()
	{
		if (!ImGui::CollapsingHeader("Shader Inspector"))
		{
			//clear selection markers when the inspector is no longer visible.
			HookD3D12::ClearShaderMarkers();
			return;
		}

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		ImGui::InputTextMultiline("##DeveloperSettingsNote",
								  const_cast<char*>(noteDeveloperSettingsText),
								  strlen(noteDeveloperSettingsText) + 1,
								  ImVec2(-FLT_MIN, 0), //-FLT_MIN width = stretch to window edge, 0 height = auto
								  ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

		ImGui::Spacing();
		DrawAdapterInfo();
		DrawD3D12PipelineInfo();
		DrawRetiredPSODiagnostics();
		ImGui::Spacing();

		const char* selectionStyles[] =
		{
			"Blue Pixel Shader",
			"Hidden",
			"None",
		};

		const char* btnLabel = "Clear Selections";
		PropertyWidgets::PrepareRow("Selection Style");

		//share the control cell between the dropdown and its clear action.
		const float clearButtonWidth = ImGui::CalcTextSize(btnLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::CalcItemWidth() - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x));

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

		DrawGraphicsPipelines();
		DrawStreamPipelines();

		ImGui::Unindent(indentSpace);
	}

	void DrawAdapterInfo()
	{
		if (ImGui::TreeNodeEx("Adapter Info"))
		{
			ImGui::SeparatorText("GPU");
			PropertyWidgets::ReadOnlyText("Adapter", "##AdapterInfoAdapter", HookD3D12::gPipelineInfo.graphicsProcessorName);
			PropertyWidgets::ReadOnlyText("Vendor ID", "##AdapterInfoVendorID", StringHelper::Format("0x%X", HookD3D12::gPipelineInfo.vendorID));
			PropertyWidgets::ReadOnlyText("Device ID", "##AdapterInfoDeviceID", StringHelper::Format("0x%X", HookD3D12::gPipelineInfo.deviceID));
			ImGui::SeparatorText("Memory");
			PropertyWidgets::ReadOnlyText("Dedicated VRAM", "##AdapterInfoDedicatedVRAM", StringHelper::Format("%.2f GB", HookD3D12::gPipelineInfo.dedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0)));
			PropertyWidgets::ReadOnlyText("Dedicated System", "##AdapterInfoDedicatedSystem", StringHelper::Format("%.2f GB", HookD3D12::gPipelineInfo.dedicatedSystemMemory / (1024.0 * 1024.0 * 1024.0)));
			PropertyWidgets::ReadOnlyText("Shared System", "##AdapterInfoSharedSystem", StringHelper::Format("%.2f GB", HookD3D12::gPipelineInfo.sharedSystemMemory / (1024.0 * 1024.0 * 1024.0)));
			ImGui::Separator();
			ImGui::TreePop();
		}
	}

	void DrawD3D12PipelineInfo()
	{
		if (ImGui::TreeNodeEx("D3D12 Pipeline Info"))
		{
			ImGui::SeparatorText("Swap Chain");
			PropertyWidgets::ReadOnlyText("Buffers", "##D3D12InfoBuffers", std::to_string(HookD3D12::gPipelineInfo.swapChainBufferCount));
			PropertyWidgets::ReadOnlyText("Format", "##D3D12InfoFormat", std::to_string(HookD3D12::gPipelineInfo.swapChainFormat));
			ImGui::SeparatorText("Feature Support");
			PropertyWidgets::ReadOnlyText("Resource Binding Tier", "##D3D12InfoResourceBindingTier", std::to_string(HookD3D12::gPipelineInfo.resourceBindingTier));
			PropertyWidgets::ReadOnlyText("Tiled Resources Tier", "##D3D12InfoTiledResourcesTier", std::to_string(HookD3D12::gPipelineInfo.tiledResourcesTier));
			PropertyWidgets::ReadOnlyText("Conservative Raster Tier", "##D3D12InfoConservativeRasterTier", std::to_string(HookD3D12::gPipelineInfo.conservativeRasterTier));
			PropertyWidgets::ReadOnlyText("Raytracing Tier", "##D3D12InfoRaytracingTier", std::to_string(HookD3D12::gPipelineInfo.raytracingTier));
			PropertyWidgets::ReadOnlyText("Mesh Shader Tier", "##D3D12InfoMeshShaderTier", std::to_string(HookD3D12::gPipelineInfo.meshShaderTier));
			ImGui::SeparatorText("Command Queue");
			PropertyWidgets::ReadOnlyText("Queue Type", "##D3D12InfoQueueType", std::to_string(HookD3D12::gPipelineInfo.commandQueueType));
			ImGui::Separator();
			ImGui::TreePop();
		}
	}

	void DrawGraphicsPipelines()
	{
		const std::string headerText = "Graphics Pipelines: " + std::to_string(HookD3D12::gGraphicsPipelines.size()) + " PSOs";
		static bool wasOpen = false;
		const bool isOpen = ImGui::CollapsingHeader(headerText.c_str());

		if (isOpen)
		{
			DrawShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::pixelShaderHash, &HookD3D12::GraphicsPipelineInfo::pixelShaderBytecodeSize, &HookD3D12::GraphicsPipelineInfo::pixelShaderBytecode>(
				"Pixel Shaders", "GraphicsPS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::PixelShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, HookD3D12::PipelineSourceList::Graphics, true, false, &HookD3D12::GraphicsPipelineInfo::pixelShaderDisabled, &HookD3D12::GraphicsPipelineInfo::pipelineStateWithoutPixelShader);

			DrawShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::vertexShaderHash, &HookD3D12::GraphicsPipelineInfo::vertexShaderBytecodeSize, &HookD3D12::GraphicsPipelineInfo::vertexShaderBytecode>(
				"Vertex Shaders", "GraphicsVS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::VertexShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, HookD3D12::PipelineSourceList::Graphics, false, false, nullptr, nullptr);

			DrawShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::geometryShaderHash, &HookD3D12::GraphicsPipelineInfo::geometryShaderBytecodeSize, &HookD3D12::GraphicsPipelineInfo::geometryShaderBytecode>(
				"Geometry Shaders", "GraphicsGS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::GeometryShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, HookD3D12::PipelineSourceList::Graphics, false, false, nullptr, nullptr);

			DrawShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::hullShaderHash, &HookD3D12::GraphicsPipelineInfo::hullShaderBytecodeSize, &HookD3D12::GraphicsPipelineInfo::hullShaderBytecode>(
				"Hull Shaders", "GraphicsHS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::HullShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, HookD3D12::PipelineSourceList::Graphics, false, false, nullptr, nullptr);

			DrawShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::domainShaderHash, &HookD3D12::GraphicsPipelineInfo::domainShaderBytecodeSize, &HookD3D12::GraphicsPipelineInfo::domainShaderBytecode>(
				"Domain Shaders", "GraphicsDS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderTarget::DomainShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, HookD3D12::PipelineSourceList::Graphics, false, false, nullptr, nullptr);
		}
		else if (wasOpen)
		{
			HookD3D12::ClearShaderMarkers();
		}

		wasOpen = isOpen;
	}

	void DrawStreamPipelines()
	{
		std::string headerText = "Stream Pipelines: " + std::to_string(HookD3D12::gPipelineStates.size()) + " PSOs";
		static bool wasOpen = false;
		const bool isOpen = ImGui::CollapsingHeader(headerText.c_str());

		if (isOpen)
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

			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::amplificationShaderHash, &HookD3D12::PipelineStateInfo::amplificationShaderBytecodeSize, &HookD3D12::PipelineStateInfo::amplificationShaderBytecode>(
				"Amplification Shaders", "StreamAS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::AmplificationShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, HookD3D12::PipelineSourceList::Stream, false, false, nullptr, nullptr);

			DrawShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::meshShaderHash, &HookD3D12::PipelineStateInfo::meshShaderBytecodeSize, &HookD3D12::PipelineStateInfo::meshShaderBytecode>(
				"Mesh Shaders", "StreamMS", "Stream", HookD3D12::gPipelineStates, ShaderTarget::MeshShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, HookD3D12::PipelineSourceList::Stream, false, false, nullptr, nullptr);
		}
		else if (wasOpen)
		{
			HookD3D12::ClearShaderMarkers();
		}

		wasOpen = isOpen;
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
		static std::string recaptureModifiedShaderId;

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

		const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;
		const std::string tableId = std::string("ShaderList##") + idPrefix;

		if (ImGui::BeginTable(tableId.c_str(), 4, tableFlags, ImVec2(0, 180)))
		{
			ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_DefaultSort, 1.7f);
			ImGui::TableSetupColumn("Bytecode Size", ImGuiTableColumnFlags_PreferSortDescending, 1.0f);
			ImGui::TableSetupColumn("Pipeline Index", ImGuiTableColumnFlags_None, 0.8f);
			ImGui::TableSetupColumn("Replacement / Status", ImGuiTableColumnFlags_NoSort, 1.3f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			//table sorting only changes the displayed indices; pipeline storage stays in capture order.
			const ImGuiTableSortSpecs* sortSpecifications = ImGui::TableGetSortSpecs();

			std::stable_sort(sortedIndices.begin(), sortedIndices.end(), [&](int leftIndex, int rightIndex)
			{
				const PipelineT& leftPipeline = pipelines[leftIndex];
				const PipelineT& rightPipeline = pipelines[rightIndex];

				if (sortSpecifications)
				{
					for (int sortIndex = 0; sortIndex < sortSpecifications->SpecsCount; ++sortIndex)
					{
						const ImGuiTableColumnSortSpecs& specification = sortSpecifications->Specs[sortIndex];
						int order = 0;

						if (specification.ColumnIndex == 0)
						{
							const uint64_t leftHash = leftPipeline.*HashMember;
							const uint64_t rightHash = rightPipeline.*HashMember;

							if (leftHash < rightHash)
								order = -1;
							else if (leftHash > rightHash)
								order = 1;
						}
						else if (specification.ColumnIndex == 1)
						{
							const SIZE_T leftSize = leftPipeline.*SizeMember;
							const SIZE_T rightSize = rightPipeline.*SizeMember;

							if (leftSize < rightSize)
								order = -1;
							else if (leftSize > rightSize)
								order = 1;
						}
						else if (specification.ColumnIndex == 2)
						{
							if (leftIndex < rightIndex)
								order = -1;
							else if (leftIndex > rightIndex)
								order = 1;
						}

						if (order == 0)
							continue;

						if (specification.SortDirection == ImGuiSortDirection_Descending)
							return order > 0;

						return order < 0;
					}
				}

				return leftIndex < rightIndex;
			});

			for (int sortedIndex : sortedIndices)
			{
				PipelineT& pipeline = pipelines[sortedIndex];
				const uint64_t hash = pipeline.*HashMember;
				const int rowReplacementIndex = HookD3D12::FindEnabledShaderTarget(hash, shaderType);
				const bool hasReplacement = rowReplacementIndex >= 0;
				const bool isSelected = sortedIndex == selectedIndex;
				std::string status = "None";

				if (hasReplacement)
				{
					status = HookD3D12::gLoadedShaderTargets[rowReplacementIndex].name;

					if (status.empty())
						status = "Replacement enabled";
				}
				else if (allowMarkerToggle && disabledMember)
				{
					if (pipeline.*disabledMember)
						status = "Marker active";
					else
						status = "Marker inactive";
				}
				else if (shaderType == ShaderTarget::MeshShader && HookD3D12::gHiddenMeshPipelineState.load(std::memory_order_acquire) == pipeline.pipelineState)
					status = "Hidden";

				ImGui::PushID(sortedIndex);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				if (hasReplacement)
				{
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
				}

				const std::string hashLabel = Hash::FormatHash(hash) + "###ShaderStage" + idPrefix + std::to_string(sortedIndex);

				if (ImGui::Selectable(hashLabel.c_str(), isSelected || hasReplacement, ImGuiSelectableFlags_SpanAllColumns))
				{
					selectedIndex = sortedIndex;

					const HookD3D12::PixelShaderSelectionStyle selectionStyle = (HookD3D12::PixelShaderSelectionStyle)gSelectionStyleIndex;

					if (selectionStyle == HookD3D12::PixelShaderSelectionStyle::None)
					{
						HookD3D12::ClearShaderMarkers();
					}
					else if (shaderType == ShaderTarget::MeshShader)
					{
						HookD3D12::ClearShaderMarkers();
						if (selectionStyle == HookD3D12::PixelShaderSelectionStyle::Hidden)
							HookD3D12::gHiddenMeshPipelineState.store(pipeline.pipelineState, std::memory_order_release);
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
					else
					{
						HookD3D12::ClearShaderMarkers();
					}
				}

				if (hasReplacement)
					ImGui::PopStyleColor(3);

				if (isSelected)
					ImGui::SetItemDefaultFocus();

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%zu bytes", (size_t)(pipeline.*SizeMember));
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%d", sortedIndex);
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(status.c_str());
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

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
		else if (shaderType == ShaderTarget::MeshShader)
		{
			const bool hidden = HookD3D12::gHiddenMeshPipelineState.load(std::memory_order_acquire) == pipeline.pipelineState;
			ImGui::Text("Selection: %s", hidden ? "hidden" : "inactive");
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

			const auto& modifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();
			const auto* recapturePackage = DatabaseModifiedShaders::FindModifiedShaderById(recaptureModifiedShaderId);
			if (!recapturePackage || recapturePackage->shaderType != shaderType)
			{
				recaptureModifiedShaderId.clear();
				for (const auto& package : modifiedShaders)
				{
					if (package.shaderType == shaderType)
					{
						recaptureModifiedShaderId = package.id;
						break;
					}
				}
				recapturePackage = DatabaseModifiedShaders::FindModifiedShaderById(recaptureModifiedShaderId);
			}
			if (recapturePackage)
			{
				const std::string comboId = "##RecaptureModifiedShader" + std::string(idPrefix);
				if (PropertyWidgets::BeginCombo("Recapture Into Modified Shader", comboId.c_str(), recapturePackage->name.c_str()))
				{
					for (const auto& package : modifiedShaders)
					{
						if (package.shaderType != shaderType)
							continue;
						const std::string label = DatabaseModifiedShaders::DisplayName(package) + "##" + package.id;
						if (ImGui::Selectable(label.c_str(), package.id == recaptureModifiedShaderId))
							recaptureModifiedShaderId = package.id;
					}
					ImGui::EndCombo();
				}
				const std::string buttonId = "Recapture Modified Shader##" + std::string(idPrefix);
				if (ImGui::Button(buttonId.c_str()))
				{
					std::string recaptureMessage;
					if (DatabaseModifiedShaders::RecaptureModifiedShader(recaptureModifiedShaderId, recaptureMessage, hash))
						WriteToRuntimeLogSuccess(recaptureMessage);
					else
						WriteToRuntimeLogError(recaptureMessage);
				}
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
