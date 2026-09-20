#pragma once

#include <cstdint>
#include <string>

#include "Enum/RenderDocCaptureRequestResult.h"
#include "Enum/RenderDocReplayUIRequestResult.h"

namespace RenderDocIntegration
{
	void Initialize();
	void Refresh();

	bool IsAvailable();
	bool IsFrameCapturing();
	bool IsTargetControlConnected();
	bool WasLoadedByInjector();
	RenderDocCaptureRequestResult RequestFrameCapture(void* d3d12Device, void* windowHandle);
	void PollCaptureStatus();
	uint64_t GetCaptureRequestSequence();
	RenderDocReplayUIRequestResult ConnectReplayUI();

	std::string GetStatusText();
	std::string GetApiVersionText();
	std::string GetLibraryPath();
	uint32_t GetCaptureCount();
	uint32_t GetReplayUiProcessId();
	std::string GetLatestCapturePath();
}
