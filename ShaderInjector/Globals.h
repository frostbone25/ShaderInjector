#pragma once

#include <windows.h>
#include <vector>

namespace Globals
{
	enum class ShaderDiscoveryMode
	{
		HashLookup = 0,
		ShaderAnalysis = 1,
	};

	// Shader profiles are configured per stage because a game can use different
	// target levels for graphics and compute workloads. Values mirror the
	// familiar profile suffixes so the serialized INI remains readable.
	enum class ShaderModel
	{
		ShaderModel5_0 = 50,
		ShaderModel5_1 = 51,
		ShaderModel6_0 = 60,
		ShaderModel6_1 = 61,
		ShaderModel6_2 = 62,
		ShaderModel6_3 = 63,
		ShaderModel6_4 = 64,
		ShaderModel6_5 = 65,
		ShaderModel6_6 = 66,
	};

	// Handle to our DLL module
	extern HMODULE mainModule;

	// Main game window handle
	extern HWND mainWindow;

	// Key to open/close the ImGui menu (INSERT by default)
	extern int keyOpenShaderInjectorGUI;

	// Key to enable/disable shader injection (HOME by default)
	extern int keyToggleShaderInjector;

	// Shared runtime state. These must be extern rather than static so every translation unit
	// observes the settings loaded from ShaderInjector.ini instead of keeping its own copy.
	extern bool gShowShaderInjectorGUI;
	extern bool gShaderInjectorEnabled;
	extern bool gRenderDocIntegrationEnabled;
	extern bool gRenderDocAutoAttachEnabled;
	// High-frequency counters and five-second performance reports. Disabled by
	// default unless explicitly requested for profiling.
	extern bool gPerformanceTelemetryEnabled;
	// When enabled, compiler profiles follow the dominant shader model observed
	// in original game PSOs. The per-stage values below remain manual fallbacks.
	extern bool gAutoDetectShaderModels;

	// Overlay scale. The overlay is drawn inside the game's swapchain at a fixed pixel size, so it
	// does not follow Windows display scaling and gets harder to read as display resolution rises.
	// 1.0 is the original size, 2.0 is comfortable at 4K. Applied once during ImGui setup.
	extern float gShaderInjectorGUIScale;

	// HLSL compiler targets used for newly generated templates and every
	// subsequent shader recompile. Shader Model 5 targets produce DXBC through
	// D3DCompiler; Shader Model 6 targets produce DXIL through DXC.
	inline ShaderModel gVertexShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gHullShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gDomainShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gGeometryShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gPixelShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gComputeShaderModel = ShaderModel::ShaderModel6_6;

	// Shader discovery tuning. WorkerThreads = 0 means automatic half-core scaling.
	extern ShaderDiscoveryMode gShaderDiscoveryMode;
	extern int gShaderDiscoveryWorkerThreads;
	extern int gShaderDiscoveryWorkerThreadPriority;
	extern int gShaderDiscoveryFrameJobBudget;
	extern int gShaderDiscoveryPendingAnalysisLimit;
	extern int gShaderDiscoveryQueuedShaderLimit;
	extern double gShaderDiscoveryMinimumSimilarityScore;
	extern double gShaderDiscoverySimilarityAmbiguityMargin;

	extern std::vector<uint8_t> nullPixelShaderBlob;
	extern std::vector<uint8_t> markerPixelShaderBlob;
	extern std::vector<uint8_t> markerComputeShaderBlob;
}   
