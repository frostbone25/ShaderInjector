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
