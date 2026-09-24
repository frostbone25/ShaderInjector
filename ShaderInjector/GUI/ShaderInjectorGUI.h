#pragma once

#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

//3RD Party
#include "imgui.h"

//custom
#include "HookD3D12.h"
#include "IO/ShaderInjectorIO.h"
#include "Hash/Hash.h"
#include "Globals.h"
#include "ShaderTarget/ShaderTarget.h"
#include "GUI/MainWindowContext.h"
#include "GUI/ModifiedShaderBatchRecompileResult.h"

namespace ShaderInjectorGUI
{
	const float indentSpace = 16.0f;

	void DrawMainWindow(const MainWindowContext& context);
	void DrawShaderInjectorMenu();

	//===================== modified shaders =====================
	ModifiedShaderBatchRecompileResult RecompileModifiedShaders(bool includeInactivePackages);

	void DrawModifiedShaders();

	//===================== shader configuration =====================
	void DrawShaderConfiguration();

	//===================== shader targets =====================
	void DrawShaderTargets();

	template <typename PipelineT>
	bool PipelineUsesReplacement(const PipelineT& pipeline, const ShaderTarget::ShaderTargetDisk& replacement);

	void DrawShaderTargetSourceSection(ShaderTarget::ShaderTargetDisk& replacement, int replacementIndex);

	template <typename PipelineT>
	void DrawReplacementPSORow(const char* sourceList, int index, const PipelineT& pipeline);
	void DrawShaderTargetPSOList(const ShaderTarget::ShaderTargetDisk& replacement);
	int CountReplacementPSOs(const ShaderTarget::ShaderTargetDisk& replacement);

	//===================== render passes =====================
	void DrawRenderPasses();
	void DrawShaderResources();

	template <typename PipelineT>
	bool PipelineUsesReplacement(const PipelineT& pipeline, const ShaderTarget::ShaderTargetDisk& replacement);

	//===================== developer settings =====================
	void DrawDeveloperSettings();
	void DrawShaderCompilerSettings();
	void DrawRenderDoc();
	void DrawAdapterInfo();
	void DrawD3D12PipelineInfo();

	//===================== pipelines =====================
	void DrawStreamPipelines();

	template <
		typename PipelineT,
		uint64_t PipelineT::*HashMember,
		SIZE_T PipelineT::*SizeMember,
		std::vector<uint8_t> PipelineT::*BytecodeMember>
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
		ID3D12PipelineState* PipelineT::*rebuiltPSOMember);

	template <typename PipelineT, uint64_t PipelineT::*HashMember>
	int CountShaderStage(const std::vector<PipelineT>& pipelines);

	template <typename PipelineT, uint64_t PipelineT::*HashMember>
	int FindFirstShaderStageIndex(const std::vector<PipelineT>& pipelines);

	//===================== runtime logs =====================
	extern std::string runtimeLogText;

	void WriteToRuntimeLog(std::string text);
	void WriteToRuntimeLogError(std::string text);
	void WriteToRuntimeLogSuccess(std::string text);
	void WriteToRuntimeLogWarning(std::string text);
	void ClearRuntimeLog();
	std::string GetRuntimeLogSnapshot();

	//===================== style =====================
	void ApplyGUIStyle();
} //namespace ShaderInjectorGUI
