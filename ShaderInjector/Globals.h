#pragma once
#include "Enum/ShaderDiscoveryMode.h"
#include "Enum/ShaderModel.h"

#include <windows.h>
#include <vector>

namespace Globals
{
	//Handle to our DLL module
	extern HMODULE mainModule;

	//Main game window handle
	extern HWND mainWindow;

	//Key to open/close the ImGui menu (INSERT by default)
	extern int keyOpenShaderInjectorGUI;

	//Key to enable/disable shader injection (HOME by default)
	extern int keyToggleShaderInjector;

	//Shared runtime state. These must be extern rather than static so every translation unit
	//observes the settings loaded from ShaderInjector.ini instead of keeping its own copy.
	extern bool gShowShaderInjectorGUI;
	extern bool gShaderInjectorEnabled;
	extern bool gRenderDocIntegrationEnabled;
	extern bool gRenderDocAutoAttachEnabled;

	//High-frequency counters and five-second performance reports. Disabled by
	//default unless explicitly requested for profiling.
	extern bool gPerformanceTelemetryEnabled;

	//Logging controls. Informational messages can be disabled independently
	//from errors, warnings, and successful operation messages.
	extern bool gDisableLogs;
	extern bool gVerboseLog;

	//List loaded package names during database refresh, independently of VerboseLog.
	extern bool gLogModifiedShaderNames;

	//When enabled, compiler profiles follow the dominant shader model observed
	//in original game PSOs. The per-stage values below remain manual fallbacks.
	extern bool gAutoDetectShaderModels;

	//Overlay scale. The overlay is drawn inside the game's swapchain at a fixed pixel size, so it
	//does not follow Windows display scaling and gets harder to read as display resolution rises.
	//1.0 is the original size, 2.0 is comfortable at 4K. Applied once during ImGui setup.
	extern float gShaderInjectorGUIScale;

	//HLSL compiler targets used for newly generated templates and every
	//subsequent shader recompile. Shader Model 5 targets produce DXBC through
	//D3DCompiler; Shader Model 6 targets produce DXIL through DXC.
	inline ShaderModel gVertexShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gHullShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gDomainShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gGeometryShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gPixelShaderModel = ShaderModel::ShaderModel6_6;
	inline ShaderModel gComputeShaderModel = ShaderModel::ShaderModel6_6;

	//Shader discovery tuning. WorkerThreads = 0 means automatic half-core scaling.
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
} //namespace Globals
