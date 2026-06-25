#pragma once

#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "imgui.h"

#include "HookD3D12.h"
#include "ShaderInjectorIO.h"
#include "Hash.h"
#include "Globals.h"
#include "ShaderReplacement.h"

namespace ShaderInjectorGUI
{
	using DrawMenuFn = void(*)();

	struct MainWindowContext
	{
		bool* showWindow = nullptr;
		bool injectorEnabled = false;
		bool injectorDeveloperSettings = false;
		bool* fpsCounterActive = nullptr;
		double fps = 0.0;
		double frameTimeMs = 0.0;
		const std::string* runtimeLogText = nullptr;
		DrawMenuFn drawMenu = nullptr;
	};

	void DrawMainWindow(const MainWindowContext& context);
	void UI_ShaderInjectorMenu();

	//===================== shader replacements =====================
	void UI_ShaderReplacements();

	template<typename PipelineT>
	bool PipelineUsesReplacement(const PipelineT& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement);

	void UI_ShaderReplacementSourceSection(ShaderReplacement::ShaderReplacementDisk& replacement, int replacementIndex);

	template<typename PipelineT>
	void UI_DrawReplacementPSORow(const char* sourceList, int index, const PipelineT& pipeline);
	void UI_ShaderReplacementPSOList(const ShaderReplacement::ShaderReplacementDisk& replacement);
	int CountReplacementPSOs(const ShaderReplacement::ShaderReplacementDisk& replacement);

	template<typename PipelineT>
	bool PipelineUsesReplacement(const PipelineT& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement);

	//===================== developer settings =====================
	void UI_AdapterInfo();
	void UI_D3D12PipelineInfo();

	//===================== pipelines =====================
	void UI_StreamPipelines();
	//void UI_GraphicsPipelines(); //<--- hidden from GUI for now

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
		ID3D12PipelineState* PipelineT::* rebuiltPSOMember);

	template<typename PipelineT, uint64_t PipelineT::* HashMember>
	int CountShaderStage(const std::vector<PipelineT>& pipelines);

	template<typename PipelineT, uint64_t PipelineT::* HashMember>
	int FindFirstShaderStageIndex(const std::vector<PipelineT>& pipelines);

	//===================== runtime logs =====================
	extern std::string runtimeLogText;

	void UI_RuntimeLogTextBox(const std::string* logText);
	void WriteToRuntimeLog(std::string text);
	void WriteToRuntimeLogError(std::string text);
	void WriteToRuntimeLogSuccess(std::string text);
	void WriteToRuntimeLogWarning(std::string text);
	void ClearRuntimeLog();
}
