#pragma once

#include <cstdint>
#include <string>

namespace RenderDocIntegration
{
	enum class CaptureRequestResult
	{
		Queued,
		Disabled,
		Unavailable,
		AlreadyCapturing,
		TargetUnavailable,
	};

	enum class ReplayUiRequestResult
	{
		Launched,
		AlreadyConnected,
		Disabled,
		Unavailable,
		LaunchFailed,
	};

	void Initialize();
	void Refresh();

	bool IsAvailable();
	bool IsFrameCapturing();
	bool IsTargetControlConnected();
	bool WasLoadedByInjector();
	CaptureRequestResult RequestFrameCapture(void* d3d12Device, void* windowHandle);
	void PollCaptureStatus();
	uint64_t GetCaptureRequestSequence();
	ReplayUiRequestResult ConnectReplayUi();

	std::string GetStatusText();
	std::string GetApiVersionText();
	std::string GetLibraryPath();
	uint32_t GetCaptureCount();
	uint32_t GetReplayUiProcessId();
	std::string GetLatestCapturePath();
}
