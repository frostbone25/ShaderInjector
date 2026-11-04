#include "GUI/ShaderInjectorGUI.h"

#include "GUI/ShaderTargetReloadResult.h"
#include "HookD3D12/HookD3D12.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "ModifiedShader/ModifiedShaderPackageDisk.h"
#include "Performance/PerformanceMetrics.h"
#include "ShaderDiscovery/ShaderAutomaticDiscovery.h"
#include "ShaderTarget/ShaderTargetDisk.h"
#include "IO/ShaderInjectorIO.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

namespace
{
	int failedAssertions = 0;
	int performanceProgressQueryCount = 0;
	ShaderAutomaticDiscovery::ShaderTargetProgress currentProgress{};
	std::vector<ModifiedShader::ModifiedShaderPackageDisk> modifiedShaderPackages;

	struct SideEffectCounters
	{
		int refreshModifiedShaders = 0;
		int ensureModifiedShadersLoaded = 0;
		int findModifiedShader = 0;
		int compileModifiedShader = 0;
		int refreshLoadedShaderTargets = 0;
		int reloadShaderTarget = 0;
		int deleteShaderTarget = 0;
		int markShaderTargetApplyDirty = 0;
		int effectiveEnableCheck = 0;
		int fileExists = 0;
		int writeTextFile = 0;
		int openDirectory = 0;
		int runtimeLogWrite = 0;
		int writeShaderTargetJson = 0;
	};

	SideEffectCounters sideEffectCounters{};

	void Check(bool condition, const char* description)
	{
		if (condition)
			return;

		std::fprintf(stderr, "FAILED: %s\n", description);
		++failedAssertions;
	}

	ID3D12PipelineState* DummyPipelinePointer(uintptr_t identity)
	{
		return reinterpret_cast<ID3D12PipelineState*>(identity);
	}

	void BeginFrame(const char* windowTitle)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(1280.0f, 900.0f);
		io.DeltaTime = 1.0f / 60.0f;
		io.MousePos = ImVec2(-100.0f, -100.0f);
		io.MouseDown[0] = false;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(1240.0f, 860.0f), ImGuiCond_Always);
		ImGui::Begin(windowTitle, nullptr, ImGuiWindowFlags_NoSavedSettings);
	}

	void EndFrame()
	{
		ImGui::End();
		ImGui::Render();
	}

	void ConfigurePipelineReplacement(
		HookD3D12::GraphicsPipelineInfo& pipeline,
		bool hasReplacement,
		ShaderTarget::ShaderType activeShaderType,
		uint64_t activeShaderHash,
		uintptr_t pointerIdentity)
	{
		if (hasReplacement)
			pipeline.pipelineStateWithReplacement = DummyPipelinePointer(pointerIdentity);

		pipeline.activeShaderTargetType = activeShaderType;
		pipeline.activeShaderTargetHash = activeShaderHash;
	}

	void ConfigurePipelineReplacement(
		HookD3D12::PipelineStateInfo& pipeline,
		bool hasReplacement,
		ShaderTarget::ShaderType activeShaderType,
		uint64_t activeShaderHash,
		uintptr_t pointerIdentity)
	{
		if (hasReplacement)
			pipeline.pipelineStateWithReplacement = DummyPipelinePointer(pointerIdentity);

		pipeline.activeShaderTargetType = activeShaderType;
		pipeline.activeShaderTargetHash = activeShaderHash;
	}

	void BuildLargePipelineCollections(uint64_t targetHash, int& expectedMatchCount)
	{
		HookD3D12::gGraphicsPipelines.clear();
		HookD3D12::gPipelineStates.clear();
		HookD3D12::gGraphicsPipelines.reserve(25000);
		HookD3D12::gPipelineStates.reserve(25000);
		expectedMatchCount = 0;

		for (int pipelineIndex = 1; pipelineIndex <= 25000; ++pipelineIndex)
		{
			HookD3D12::GraphicsPipelineInfo pipeline{};
			const bool hasExactIdentity = pipelineIndex % 997 == 0;
			const bool hasReplacement = pipelineIndex % 13 != 0;
			ShaderTarget::ShaderType activeShaderType = ShaderTarget::VertexShader;
			uint64_t activeShaderHash = targetHash;

			if (hasExactIdentity)
			{
				activeShaderType = ShaderTarget::PixelShader;
				if (hasReplacement)
					++expectedMatchCount;
			}
			else if (pipelineIndex % 2 == 0)
			{
				activeShaderType = ShaderTarget::VertexShader;
			}
			else
			{
				activeShaderType = ShaderTarget::PixelShader;
				activeShaderHash ^= 0x80;
			}

			ConfigurePipelineReplacement(pipeline, hasReplacement, activeShaderType, activeShaderHash, static_cast<uintptr_t>(pipelineIndex));
			HookD3D12::gGraphicsPipelines.push_back(std::move(pipeline));
		}

		for (int pipelineIndex = 1; pipelineIndex <= 25000; ++pipelineIndex)
		{
			HookD3D12::PipelineStateInfo pipeline{};
			const bool hasExactIdentity = pipelineIndex % 601 == 0;
			const bool hasReplacement = pipelineIndex % 19 != 0;
			ShaderTarget::ShaderType activeShaderType = ShaderTarget::GeometryShader;
			uint64_t activeShaderHash = targetHash;

			if (hasExactIdentity)
			{
				activeShaderType = ShaderTarget::PixelShader;
				if (hasReplacement)
					++expectedMatchCount;
			}
			else if (pipelineIndex % 2 == 0)
			{
				activeShaderType = ShaderTarget::GeometryShader;
			}
			else
			{
				activeShaderType = ShaderTarget::PixelShader;
				activeShaderHash ^= 0x400;
			}

			ConfigurePipelineReplacement(pipeline, hasReplacement, activeShaderType, activeShaderHash, static_cast<uintptr_t>(pipelineIndex + 30000));
			HookD3D12::gPipelineStates.push_back(std::move(pipeline));
		}
	}

	void ResetSideEffectCounters()
	{
		sideEffectCounters = {};
	}

	bool NoForbiddenDefaultUIEffects()
	{
		return sideEffectCounters.refreshModifiedShaders == 0 &&
			sideEffectCounters.compileModifiedShader == 0 &&
			sideEffectCounters.refreshLoadedShaderTargets == 0 &&
			sideEffectCounters.reloadShaderTarget == 0 &&
			sideEffectCounters.deleteShaderTarget == 0 &&
			sideEffectCounters.markShaderTargetApplyDirty == 0 &&
			sideEffectCounters.effectiveEnableCheck == 0 &&
			sideEffectCounters.fileExists == 0 &&
			sideEffectCounters.writeTextFile == 0 &&
			sideEffectCounters.openDirectory == 0 &&
			sideEffectCounters.runtimeLogWrite == 0 &&
			sideEffectCounters.writeShaderTargetJson == 0;
	}

	void ConfigureShaderTarget(ShaderTarget::ShaderTargetDisk& replacement, const std::string& hashText)
	{
		replacement.enabled = false;
		replacement.name = "UI Regression Target";
		replacement.shaderType = ShaderTarget::PixelShader;
		replacement.shaderProfile = "ps_6_6";
		replacement.shaderEntryPoint = "main";
		replacement.originalShaderBytecodeHash = hashText;
		replacement.modifiedShaderId = "missing-package-is-a-read-only-lookup";
	}

	void CheckLargePipelineCountAndBenchmark(ShaderTarget::ShaderTargetDisk& replacement, int expectedMatchCount)
	{
		const int actualMatchCount = ShaderInjectorGUI::CountReplacementPSOs(replacement);
		Check(actualMatchCount == expectedMatchCount, "large graphics and stream collections count only exact active target matches");
		Check(sideEffectCounters.effectiveEnableCheck == 0, "PSO count does not query package readiness or effective enabled state");

		const auto benchmarkStart = std::chrono::steady_clock::now();
		int countedTotal = 0;
		for (int iteration = 0; iteration < 100; ++iteration)
			countedTotal += ShaderInjectorGUI::CountReplacementPSOs(replacement);
		const auto benchmarkEnd = std::chrono::steady_clock::now();
		const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(benchmarkEnd - benchmarkStart).count();

		Check(countedTotal == expectedMatchCount * 100, "repeated large PSO counts remain stable");
		std::printf("PSO count benchmark: %d records x 100 iterations, %.3f ms total (%.3f ms per iteration)\n",
			static_cast<int>(HookD3D12::gGraphicsPipelines.size() + HookD3D12::gPipelineStates.size()),
			elapsedMilliseconds,
			elapsedMilliseconds / 100.0);
	}

	void CheckEmptyTargetProgressAndIndentation()
	{
		HookD3D12::gLoadedShaderTargets.clear();
		HookD3D12::gLoadedShaderTargetsOnce = true;
		HookD3D12::gSelectedShaderTargetIndex = -1;
		currentProgress = {8, 2, 1, 0, 5, 1};
		ResetSideEffectCounters();
		const int previousProgressQueries = performanceProgressQueryCount;

		BeginFrame("Shader Target Empty List Validation");
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiID headerID = window->GetID("###ShaderTargets");
		window->DC.StateStorage->SetInt(headerID, 1);
		const float cursorXBeforeDraw = ImGui::GetCursorPosX();
		const int verticesBeforeDraw = window->DrawList->VtxBuffer.Size;
		ShaderInjectorGUI::DrawShaderTargets();
		const float cursorXAfterDraw = ImGui::GetCursorPosX();
		const int verticesAfterDraw = window->DrawList->VtxBuffer.Size;
		EndFrame();

		Check(performanceProgressQueryCount == previousProgressQueries + 1, "empty-target header queries discovery progress once");
		Check(verticesAfterDraw > verticesBeforeDraw, "empty-target header renders active discovery progress UI");
		Check(std::fabs(cursorXAfterDraw - cursorXBeforeDraw) < 0.5f, "empty-target early return restores the previous horizontal indentation");
		Check(sideEffectCounters.refreshLoadedShaderTargets == 0, "loaded empty target list does not refresh on each UI frame");
		Check(NoForbiddenDefaultUIEffects(), "empty-target UI does not compile, reload, open folders, or mutate targets by default");

		currentProgress = {};
		BeginFrame("Shader Target Empty List Validation");
		window = ImGui::GetCurrentWindow();
		window->DC.StateStorage->SetInt(window->GetID("###ShaderTargets"), 1);
		const int verticesWithoutProgressStart = window->DrawList->VtxBuffer.Size;
		ShaderInjectorGUI::DrawShaderTargets();
		const int verticesWithoutProgressEnd = window->DrawList->VtxBuffer.Size;
		EndFrame();

		Check(verticesAfterDraw - verticesBeforeDraw > verticesWithoutProgressEnd - verticesWithoutProgressStart,
			"nonzero discovery progress adds progress-bar drawing to the empty-target header");
		Check(performanceProgressQueryCount == previousProgressQueries + 2, "empty-target header refreshes progress exactly once per frame");
		Check(sideEffectCounters.refreshLoadedShaderTargets == 0, "empty-target list remains cached across frames");
	}

	void CheckExpandedTargetUIAndStablePSOTree(ShaderTarget::ShaderTargetDisk& replacement, int expectedMatchCount)
	{
		HookD3D12::gLoadedShaderTargets.clear();
		HookD3D12::gLoadedShaderTargets.push_back(replacement);
		HookD3D12::gLoadedShaderTargetsOnce = true;
		HookD3D12::gSelectedShaderTargetIndex = 0;
		HookD3D12::gShaderTargetNameBufferIndex = 0;
		strcpy_s(HookD3D12::gShaderTargetNameBuffer, "UI Regression Target");
		HookD3D12::gLoadedShaderTargetBlobs.clear();
		currentProgress = {8, 2, 1, 0, 5, 1};
		ResetSideEffectCounters();
		const int previousProgressQueries = performanceProgressQueryCount;
		const std::string originalName = HookD3D12::gLoadedShaderTargets[0].name;
		const std::string originalHash = HookD3D12::gLoadedShaderTargets[0].originalShaderBytecodeHash;
		const std::string originalModifiedShaderID = HookD3D12::gLoadedShaderTargets[0].modifiedShaderId;
		const bool originalEnabled = HookD3D12::gLoadedShaderTargets[0].enabled;

		BeginFrame("Shader Target Selected List Validation");
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		window->DC.StateStorage->SetInt(window->GetID("###ShaderTargets"), 1);
		const float cursorXBeforeDraw = ImGui::GetCursorPosX();
		ShaderInjectorGUI::DrawShaderTargets();
		const float cursorXAfterDraw = ImGui::GetCursorPosX();
		EndFrame();

		Check(performanceProgressQueryCount == previousProgressQueries + 1, "selected-target header queries progress once");
		Check(std::fabs(cursorXAfterDraw - cursorXBeforeDraw) < 0.5f, "selected-target UI restores the previous horizontal indentation");
		Check(sideEffectCounters.ensureModifiedShadersLoaded == 1, "selected-target source panel performs one cheap package-load check");
		Check(sideEffectCounters.findModifiedShader >= 1, "selected-target source panel performs package lookups without compiling");
		Check(NoForbiddenDefaultUIEffects(), "selected-target UI performs no compile, refresh, reload, file, folder, or mutation action by default");
		Check(sideEffectCounters.effectiveEnableCheck == 0, "selected-target and PSO UI never checks package readiness per PSO");
		Check(HookD3D12::gLoadedShaderTargets[0].name == originalName &&
			HookD3D12::gLoadedShaderTargets[0].originalShaderBytecodeHash == originalHash &&
			HookD3D12::gLoadedShaderTargets[0].modifiedShaderId == originalModifiedShaderID &&
			HookD3D12::gLoadedShaderTargets[0].enabled == originalEnabled,
			"selected-target UI leaves target fields unchanged without user input");

		HookD3D12::gGraphicsPipelines.erase(HookD3D12::gGraphicsPipelines.begin());
		HookD3D12::GraphicsPipelineInfo addedMatchingPipeline{};
		ConfigurePipelineReplacement(addedMatchingPipeline, true, replacement.shaderType,
			Hash::ParseHashText(replacement.originalShaderBytecodeHash), 900001);
		HookD3D12::gGraphicsPipelines.push_back(std::move(addedMatchingPipeline));
		const int changedMatchCount = ShaderInjectorGUI::CountReplacementPSOs(replacement);
		Check(changedMatchCount == expectedMatchCount + 1, "test changes the PSO match count before exercising the stable subtree ID");

		BeginFrame("Shader Target PSO Tree Validation");
		window = ImGui::GetCurrentWindow();
		const ImGuiID stableTreeID = window->GetID("###ShaderTargetPSOList");
		window->DC.StateStorage->SetInt(stableTreeID, 1);
		ShaderInjectorGUI::DrawShaderTargetPSOList(replacement);
		const int expandedStateAfterFirstDraw = window->DC.StateStorage->GetInt(stableTreeID, 0);
		EndFrame();

		BeginFrame("Shader Target PSO Tree Validation");
		window = ImGui::GetCurrentWindow();
		const ImGuiID treeIDAfterCountChange = window->GetID("###ShaderTargetPSOList");
		Check(treeIDAfterCountChange == stableTreeID, "PSO tree ID remains stable while the visible replacement count changes");
		ShaderInjectorGUI::DrawShaderTargetPSOList(replacement);
		const int expandedStateAfterCountChange = window->DC.StateStorage->GetInt(treeIDAfterCountChange, 0);
		EndFrame();

		Check(expandedStateAfterFirstDraw == 1 && expandedStateAfterCountChange == 1,
			"expanded PSO subtree remains open after the visible count changes");
		Check(sideEffectCounters.effectiveEnableCheck == 0, "PSO tree enumeration does not perform per-PSO readiness checks");
	}
}

namespace HookD3D12
{
	std::mutex gPipelineMutex;
	std::vector<GraphicsPipelineInfo> gGraphicsPipelines;
	std::vector<PipelineStateInfo> gPipelineStates;
	std::vector<ShaderTarget::ShaderTargetDisk> gLoadedShaderTargets;
	std::vector<std::vector<uint8_t>> gLoadedShaderTargetBlobs;
	int gSelectedShaderTargetIndex = -1;
	int gShaderTargetNameBufferIndex = -1;
	char gShaderTargetNameBuffer[256]{};
	bool gLoadedShaderTargetsOnce = false;

	void RefreshLoadedShaderTargets()
	{
		++sideEffectCounters.refreshLoadedShaderTargets;
	}

	void SyncShaderTargetNameBuffer()
	{
	}

	void MarkShaderTargetApplyDirty()
	{
		++sideEffectCounters.markShaderTargetApplyDirty;
	}

	bool ReloadShaderTarget(int)
	{
		++sideEffectCounters.reloadShaderTarget;
		return false;
	}

	bool DeleteShaderTarget(int)
	{
		++sideEffectCounters.deleteShaderTarget;
		return false;
	}

	bool IsShaderTargetEffectivelyEnabled(const ShaderTarget::ShaderTargetDisk&)
	{
		++sideEffectCounters.effectiveEnableCheck;
		return false;
	}
}

namespace DatabaseModifiedShaders
{
	void RefreshModifiedShaders()
	{
		++sideEffectCounters.refreshModifiedShaders;
	}

	void EnsureModifiedShadersLoaded()
	{
		++sideEffectCounters.ensureModifiedShadersLoaded;
	}

	const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& GetModifiedShaders()
	{
		return modifiedShaderPackages;
	}

	const ModifiedShader::ModifiedShaderPackageDisk* FindModifiedShaderById(const std::string&)
	{
		++sideEffectCounters.findModifiedShader;
		return nullptr;
	}

	std::string DisplayName(const ModifiedShader::ModifiedShaderPackageDisk& package)
	{
		if (package.name.empty())
			return package.id;

		return package.name;
	}

	bool CompileModifiedShader(const std::string&, bool)
	{
		++sideEffectCounters.compileModifiedShader;
		return false;
	}

	bool HasCompiledVariant(const ModifiedShader::ModifiedShaderPackageDisk&)
	{
		return false;
	}
}

namespace ModifiedShader
{
	bool ModifiedShaderPackageDisk::HasPerTargetDefines() const
	{
		return false;
	}
}

namespace ShaderAutomaticDiscovery
{
	ShaderTargetProgress GetProgress()
	{
		++performanceProgressQueryCount;
		return currentProgress;
	}
}

namespace ShaderInjectorIO
{
	bool FileExists(const std::string&)
	{
		++sideEffectCounters.fileExists;
		return false;
	}

	bool WriteTextFile(const std::string&, const std::string&)
	{
		++sideEffectCounters.writeTextFile;
		return false;
	}

	bool OpenDirectory(const std::string&)
	{
		++sideEffectCounters.openDirectory;
		return false;
	}

	std::string GetShaderTargetsDirectory()
	{
		return "test-shader-targets";
	}

	std::string GetModifiedShadersDirectory()
	{
		return "test-modified-shaders";
	}
}

namespace ShaderTarget
{
	bool WriteShaderTargetJson(const ShaderTargetDisk&)
	{
		++sideEffectCounters.writeShaderTargetJson;
		return false;
	}
}

namespace PerformanceMetrics
{
	ScopedTimer::ScopedTimer(Timing, uint32_t)
	{
	}

	ScopedTimer::~ScopedTimer()
	{
	}
}

namespace ShaderInjectorGUI
{
	std::string runtimeLogText;

	void WriteToRuntimeLogError(std::string)
	{
		++sideEffectCounters.runtimeLogWrite;
	}

	void WriteToRuntimeLogSuccess(std::string)
	{
		++sideEffectCounters.runtimeLogWrite;
	}

	void WriteToRuntimeLogWarning(std::string)
	{
		++sideEffectCounters.runtimeLogWrite;
	}
}

namespace StringHelper
{
	std::string ShaderTypeToString(ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
		case ShaderTarget::PixelShader:
			return "Pixel Shader";
		case ShaderTarget::VertexShader:
			return "Vertex Shader";
		case ShaderTarget::GeometryShader:
			return "Geometry Shader";
		default:
			return "Unknown Shader";
		}
	}
}

int main()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui::GetIO().IniFilename = nullptr;
	unsigned char* fontPixels = nullptr;
	int fontWidth = 0;
	int fontHeight = 0;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

	constexpr uint64_t targetHash = 0xAABBCCDD11223344;
	ShaderTarget::ShaderTargetDisk replacement{};
	ConfigureShaderTarget(replacement, Hash::FormatHash(targetHash));
	int expectedMatchCount = 0;
	BuildLargePipelineCollections(targetHash, expectedMatchCount);

	CheckLargePipelineCountAndBenchmark(replacement, expectedMatchCount);
	CheckEmptyTargetProgressAndIndentation();
	CheckExpandedTargetUIAndStablePSOTree(replacement, expectedMatchCount);

	for (const HookD3D12::GraphicsPipelineInfo& pipeline : HookD3D12::gGraphicsPipelines)
		Check(pipeline.capturedPipelineLifetime.Get() == nullptr, "dummy graphics pipeline pointers do not acquire COM references");

	for (const HookD3D12::PipelineStateInfo& pipeline : HookD3D12::gPipelineStates)
		Check(pipeline.capturedPipelineLifetime.Get() == nullptr, "dummy stream pipeline pointers do not acquire COM references");

	ImGui::DestroyContext();
	if (failedAssertions > 0)
	{
		std::fprintf(stderr, "%d shader-target UI assertion(s) failed.\n", failedAssertions);
		return EXIT_FAILURE;
	}

	std::puts("All shader-target UI assertions passed.");
	return EXIT_SUCCESS;
}
