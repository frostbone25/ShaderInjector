#include "ShaderInjectorGUI.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "imgui.h"

#include "HookD3D12.h"
#include "ShaderInjectorIO.h"
#include "Hash.h"
#include "Globals.h"
#include "ShaderInjectorGUIShaderSources.h"

namespace ShaderInjectorGUI
{
	static const float indentSpace = 24.0f;
	std::string runtimeLogText;
	static bool injectorDeveloperSettings = false;
	static int gSelectionStyleIndex = (int)HookD3D12::ShaderSelectionStyle::BluePixelShader;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_MainWindow(const MainWindowContext& context)
	{
		if (!context.showWindow || !*context.showWindow)
			return;

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(25, 25), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Shader Injector", context.showWindow, flags))
		{
			ImGui::Text("Injector %s", context.injectorEnabled ? "Enabled!" : "Disabled!");

			if (context.fpsCounterActive)
			{
				ImGui::Checkbox("FPS: ", context.fpsCounterActive);

				if (*context.fpsCounterActive)
				{
					ImGui::SameLine();
					ImGui::Text("%.1f (%.4fms)", context.fps, context.frameTimeMs);
				}
			}

			ImGui::Spacing();

			if (context.drawMenu)
				context.drawMenu();

			ImGui::BeginGroup();
			ImGui::SeparatorText("Log");

			if (ImGui::TreeNodeEx("Runtime Log"))
			{
				if (ImGui::Button("Clear Log"))
					ClearRuntimeLog();

				//DrawRuntimeLogTextBox(context.runtimeLogText);

				if (context.runtimeLogText)
					ImGui::TextUnformatted(context.runtimeLogText->c_str());

				ImGui::TreePop();
			}

			ImGui::EndGroup();
		}

		ImGui::End();
	}

	void UI_ShaderInjectorMenu()
	{
		ImGui::BeginGroup();
		UI_ShaderReplacements();
		ImGui::EndGroup();

		ImGui::BeginGroup();
		if (ImGui::CollapsingHeader("Developer Settings"))
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			UI_AdapterInfo();
			UI_D3D12PipelineInfo();

			ImGui::Spacing();

			const char* selectionStyles[] =
			{
				"Blue Pixel Shader",
				"Hidden",
				"None",
			};

			ImGui::Text("Selection Style");
			ImGui::SameLine();
			if (ImGui::Combo("##SelectionStyle", &gSelectionStyleIndex, selectionStyles, IM_ARRAYSIZE(selectionStyles)))
			{
				HookD3D12::gShaderSelectionStyle = (HookD3D12::ShaderSelectionStyle)gSelectionStyleIndex;
				HookD3D12::ClearShaderMarkers();
				HookD3D12::InvalidateShaderMarkerPSOs();
			}

			ImGui::Spacing();

			UI_StreamPipelines();

			//NOTE: hidden from GUI for now, even though the app can largly support operations
			//with the graphics pipeline, for the most part with the game, FF7 rebirth we are primarily
			//going to be messing with the stream pipeline, and this also will help avoid confusion for users
			//UI_GraphicsPipelines();

			ImGui::Spacing();
			ImGui::Unindent(indentSpace);
		}
		else
		{
			HookD3D12::ClearShaderMarkers();
		}
		ImGui::EndGroup();
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void UI_ShaderReplacements()
	{
		if (ImGui::CollapsingHeader("Shader Replacements"))
		{
			ImGui::Indent(indentSpace);
			ImGui::Spacing();

			if (!HookD3D12::gLoadedShaderReplacementsOnce)
				HookD3D12::RefreshLoadedShaderReplacements();

			ImGui::Text("Loaded: %zu", HookD3D12::gLoadedShaderReplacements.size());

			ImGui::SameLine();

			if (ImGui::Button("Refresh Shader Replacements"))
				HookD3D12::RefreshLoadedShaderReplacements();

			if (HookD3D12::gLoadedShaderReplacements.empty())
			{
				ImGui::Text("No replacement JSON files found.");
				return;
			}

			if (HookD3D12::gSelectedShaderReplacementIndex < 0 || HookD3D12::gSelectedShaderReplacementIndex >= (int)HookD3D12::gLoadedShaderReplacements.size())
				HookD3D12::gSelectedShaderReplacementIndex = 0;

			std::string childLabel = "ShaderReplacementList##ShaderReplacements";

			if (ImGui::BeginChild(childLabel.c_str(), ImVec2(0, 180), ImGuiChildFlags_Borders))
			{
				for (int i = 0; i < (int)HookD3D12::gLoadedShaderReplacements.size(); i++)
				{
					const ShaderReplacement::ShaderReplacementDisk& replacement = HookD3D12::gLoadedShaderReplacements[i];
					std::string label = replacement.name.empty() ? replacement.jsonPath : replacement.name;

					if (!replacement.originalShaderBytecodeHash.empty())
						label += " [" + replacement.originalShaderBytecodeHash + "]";

					if (!replacement.enabled)
						label += " (disabled)";

					label += "##ShaderReplacement" + std::to_string(i);

					const bool isSelected = i == HookD3D12::gSelectedShaderReplacementIndex;

					if (replacement.enabled)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
					}

					if (ImGui::Selectable(label.c_str(), isSelected || replacement.enabled))
					{
						HookD3D12::gSelectedShaderReplacementIndex = i;
						HookD3D12::gShaderReplacementNameBufferIndex = -1;
					}

					if (replacement.enabled)
						ImGui::PopStyleColor(3);

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndChild();

			if (HookD3D12::gSelectedShaderReplacementIndex < 0 || HookD3D12::gSelectedShaderReplacementIndex >= (int)HookD3D12::gLoadedShaderReplacements.size())
				return;

			HookD3D12::SyncShaderReplacementNameBuffer();

			ShaderReplacement::ShaderReplacementDisk& replacement = HookD3D12::gLoadedShaderReplacements[HookD3D12::gSelectedShaderReplacementIndex];
			const size_t loadedBlobSize = HookD3D12::gSelectedShaderReplacementIndex < (int)HookD3D12::gLoadedShaderReplacementBlobs.size()
				? HookD3D12::gLoadedShaderReplacementBlobs[HookD3D12::gSelectedShaderReplacementIndex].size()
				: 0;

			//========================== SELECTED ===========================
			ImGui::SeparatorText(HookD3D12::gShaderReplacementNameBuffer);
			ImGui::Indent(indentSpace);

			ImGui::Text("Enabled");
			ImGui::SameLine();
			if (ImGui::Checkbox("##ShaderReplacementEnable", &replacement.enabled))
				HookD3D12::MarkShaderReplacementApplyDirty();
			ImGui::SameLine();

			if (ImGui::Button("Delete"))
			{
				HookD3D12::DeleteShaderReplacement(HookD3D12::gSelectedShaderReplacementIndex);
				return;
			}

			ImGui::Text("Name");
			ImGui::SameLine();
			if (ImGui::InputText("##ShaderReplacementName", HookD3D12::gShaderReplacementNameBuffer, sizeof(HookD3D12::gShaderReplacementNameBuffer)))
				replacement.name = HookD3D12::gShaderReplacementNameBuffer;

			ImGui::Spacing();

			ImGui::Text("Shader Type: %s", ShaderReplacement::ShaderTypeToString(replacement.shaderType).c_str());

			ImGui::Spacing();
			ImGui::Unindent(indentSpace);

			DrawShaderReplacementSourceSection(replacement, HookD3D12::gSelectedShaderReplacementIndex);

			ImGui::Spacing();

			if (ImGui::TreeNodeEx("Info##ShaderReplacementInfo"))
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
				ImGui::Text("Source Name: %s", replacement.shaderSourceName.c_str());
				ImGui::Text("Source: %s", replacement.shaderSourcePath.c_str());
				ImGui::Text("Modified Blob: %s", replacement.modifiedShaderBlobPath.c_str());
				ImGui::Separator();
				ImGui::TreePop();
			}

			ImGui::Spacing();

			UI_ShaderReplacementPSOList(replacement);

			ImGui::Spacing();

			ImGui::Unindent(indentSpace);
		}
	}

	void UI_ShaderReplacementPSOList(const ShaderReplacement::ShaderReplacementDisk& replacement)
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

			if (ImGui::BeginChild("ReplacementPSOList##SelectedShaderReplacement", ImVec2(0, 130), ImGuiChildFlags_Borders))
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
		ImGui::Text("Type: %s", ShaderReplacement::ShaderTypeToString(pipeline.activeShaderReplacementType).c_str());
		ImGui::Text("Hash: %s", Hash::FormatHash(pipeline.activeShaderReplacementHash).c_str());

		if (pipeline.activeShaderReplacementUsesFallback)
		{
			ImGui::SameLine();
			ImGui::Text("(fallback)");
		}

		ImGui::Separator();
	}

	int CountReplacementPSOs(const ShaderReplacement::ShaderReplacementDisk& replacement)
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
	bool PipelineUsesReplacement(const PipelineT& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		if (!replacement.enabled || !pipeline.psoWithReplacement)
			return false;

		return pipeline.activeShaderReplacementType == replacement.shaderType
			&& pipeline.activeShaderReplacementHash == Hash::ParseHashText(replacement.originalShaderBytecodeHash);
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
				"Pixel Shaders", "GraphicsPS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderReplacement::PixelShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, true, &HookD3D12::GraphicsPipelineInfo::psDisabled, &HookD3D12::GraphicsPipelineInfo::psoWithoutPS);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::vsHash, &HookD3D12::GraphicsPipelineInfo::vsSize, &HookD3D12::GraphicsPipelineInfo::vsBytecode>(
				"Vertex Shaders", "GraphicsVS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderReplacement::VertexShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, nullptr, nullptr);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::gsHash, &HookD3D12::GraphicsPipelineInfo::gsSize, &HookD3D12::GraphicsPipelineInfo::gsBytecode>(
				"Geometry Shaders", "GraphicsGS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderReplacement::GeometryShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, nullptr, nullptr);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::hsHash, &HookD3D12::GraphicsPipelineInfo::hsSize, &HookD3D12::GraphicsPipelineInfo::hsBytecode>(
				"Hull Shaders", "GraphicsHS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderReplacement::HullShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, nullptr, nullptr);

			UI_ShaderStageList<HookD3D12::GraphicsPipelineInfo, &HookD3D12::GraphicsPipelineInfo::dsHash, &HookD3D12::GraphicsPipelineInfo::dsSize, &HookD3D12::GraphicsPipelineInfo::dsBytecode>(
				"Domain Shaders", "GraphicsDS", "Graphics", HookD3D12::gGraphicsPipelines, ShaderReplacement::DomainShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, HookD3D12::PSOPendingRebuild::SourceList::Graphics, false, nullptr, nullptr);
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
				"Pixel Shaders", "StreamPS", "Stream", HookD3D12::gPipelineStates, ShaderReplacement::PixelShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, &HookD3D12::PipelineStateInfo::psDisabled, &HookD3D12::PipelineStateInfo::psoWithoutPS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::csHash, &HookD3D12::PipelineStateInfo::csSize, &HookD3D12::PipelineStateInfo::csBytecode>(
				"Compute Shaders", "StreamCS", "Stream", HookD3D12::gPipelineStates, ShaderReplacement::ComputeShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, &HookD3D12::PipelineStateInfo::csDisabled, &HookD3D12::PipelineStateInfo::psoWithoutCS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::vsHash, &HookD3D12::PipelineStateInfo::vsSize, &HookD3D12::PipelineStateInfo::vsBytecode>(
				"Vertex Shaders", "StreamVS", "Stream", HookD3D12::gPipelineStates, ShaderReplacement::VertexShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, &HookD3D12::PipelineStateInfo::vsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutVS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::gsHash, &HookD3D12::PipelineStateInfo::gsSize, &HookD3D12::PipelineStateInfo::gsBytecode>(
				"Geometry Shaders", "StreamGS", "Stream", HookD3D12::gPipelineStates, ShaderReplacement::GeometryShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, &HookD3D12::PipelineStateInfo::gsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutGS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::hsHash, &HookD3D12::PipelineStateInfo::hsSize, &HookD3D12::PipelineStateInfo::hsBytecode>(
				"Hull Shaders", "StreamHS", "Stream", HookD3D12::gPipelineStates, ShaderReplacement::HullShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, &HookD3D12::PipelineStateInfo::hsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutHS);

			UI_ShaderStageList<HookD3D12::PipelineStateInfo, &HookD3D12::PipelineStateInfo::dsHash, &HookD3D12::PipelineStateInfo::dsSize, &HookD3D12::PipelineStateInfo::dsBytecode>(
				"Domain Shaders", "StreamDS", "Stream", HookD3D12::gPipelineStates, ShaderReplacement::DomainShader, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, HookD3D12::PSOPendingRebuild::SourceList::Stream, true, &HookD3D12::PipelineStateInfo::dsDisabled, &HookD3D12::PipelineStateInfo::psoWithoutDS);
		}
		else
		{
			HookD3D12::ClearShaderMarkers();
		}
	}

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
		ShaderReplacement::ShaderType shaderType,
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE subobjectType,
		HookD3D12::PSOPendingRebuild::SourceList pendingSource,
		bool allowMarkerToggle,
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
				const int rowReplacementIndex = HookD3D12::FindEnabledShaderReplacement(hash, shaderType);
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

					const HookD3D12::ShaderSelectionStyle selectionStyle = (HookD3D12::ShaderSelectionStyle)gSelectionStyleIndex;

					if (selectionStyle == HookD3D12::ShaderSelectionStyle::None)
					{
						HookD3D12::ClearShaderMarkers();
					}
					else if (allowMarkerToggle && disabledMember && rebuiltPSOMember)
					{
						HookD3D12::gShaderSelectionStyle = selectionStyle;
						HookD3D12::ClearShaderMarkers();
						pipeline.*disabledMember = true;
						HookD3D12::MarkShaderReplacementApplyDirty();

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

		const int selectedReplacementIndex = HookD3D12::FindEnabledShaderReplacement(hash, shaderType);
		const char* selectedReplacementName = selectedReplacementIndex >= 0 ? HookD3D12::gLoadedShaderReplacements[selectedReplacementIndex].name.c_str() : "(none)";

		ImGui::SeparatorText("Selected Shader");
		ImGui::Text("Pipeline Index: %d", selectedIndex);
		ImGui::Text("Hash: %s", Hash::FormatHash(hash).c_str());
		ImGui::Text("Bytecode Length: %zu", (size_t)bytecodeSize);
		ImGui::Text("Shader Replacement: %s", selectedReplacementName);
		ImGui::Text("PSO: %p", pipeline.pipelineState);

		if (allowMarkerToggle && disabledMember && rebuiltPSOMember)
		{
			ImGui::Text("Marker: %s", pipeline.*disabledMember ? "active" : "inactive");

			if ((HookD3D12::ShaderSelectionStyle)gSelectionStyleIndex == HookD3D12::ShaderSelectionStyle::None)
				ImGui::Text("Selection Style is None.");
		}

		if (bytecode.empty())
		{
			ImGui::Text("Bytecode was not captured for this stage.");
			ImGui::TreePop();
			ImGui::Separator();
			return;
		}

		std::string dumpButtonLabel = std::string("Dump Bytecode##") + idPrefix;
		if (ImGui::Button(dumpButtonLabel.c_str()))
		{
			ShaderInjectorIO::DumpShaderBytecode(bytecode.data(), bytecode.size(), hash, ShaderReplacement::ShaderTypeToString(shaderType), ShaderInjectorIO::GetDumpsDirectory());
		}

		ImGui::SameLine();
		std::string createTemplateButtonLabel = std::string("Create Replacement Shader Template##") + idPrefix;
		if (ImGui::Button(createTemplateButtonLabel.c_str()))
		{
			HookD3D12::CreateReplacementShaderTemplateForPipeline(
				sourceList,
				selectedIndex,
				shaderType,
				hash,
				bytecode.size(),
				bytecode.data(),
				pipeline);
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

	void UI_RuntimeLogTextBox(const std::string* logText)
	{
		static std::string cachedLogText;
		static std::vector<char> logBuffer;

		if (!logText)
			return;

		if (cachedLogText != *logText)
		{
			cachedLogText = *logText;
			logBuffer.assign(cachedLogText.begin(), cachedLogText.end());
			logBuffer.push_back('\0');
		}

		if (logBuffer.empty())
			logBuffer.push_back('\0');

		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float logHeight = available.y > 140.0f ? available.y : 140.0f;
		const ImGuiInputTextFlags flags =
			ImGuiInputTextFlags_ReadOnly |
			ImGuiInputTextFlags_NoHorizontalScroll;

		ImGui::InputTextMultiline(
			"##RuntimeLogText",
			logBuffer.data(),
			logBuffer.size(),
			ImVec2(-FLT_MIN, logHeight),
			flags);
	}

	void WriteToRuntimeLog(std::string text)
	{
		runtimeLogText += "\n";
		runtimeLogText += text;
		ShaderInjectorIO::WriteToLogFile(text);
	}

	void ClearRuntimeLog()
	{
		runtimeLogText = "";
	}
}
