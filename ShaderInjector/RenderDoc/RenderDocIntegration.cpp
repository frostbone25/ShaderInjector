#include "RenderDoc/RenderDocIntegration.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include "Globals.h"
#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"
#include "renderdoc_app.h"

#include "Enum/RenderDocAvailability.h"

namespace RenderDocIntegration
{
	//render doc state is shared by the public functions below and guarded by gRenderDocMutex. 
	//keeping it private to this translation unit prevents other modules from accidentally mutating the capture state behind the API.
	std::mutex gRenderDocMutex;
	HMODULE gRenderDocModule = nullptr;
	RENDERDOC_API_1_6_0* gRenderDocApi = nullptr;
	RenderDocAvailability gAvailability = RenderDocAvailability::InstallationNotFound;
	bool gRenderDocLoadedByInjector = false;
	std::string gRenderDocLibraryPath;
	DWORD gRenderDocLoadError = ERROR_SUCCESS;
	uint32_t gReplayUiProcessId = 0;
	int gApiMajorVersion = 0;
	int gApiMinorVersion = 0;
	int gApiPatchVersion = 0;
	std::atomic<uint64_t> gCaptureRequestSequence = 0;
	bool gCaptureRequestPending = false;
	bool gCaptureStartedForPendingRequest = false;
	uint32_t gCaptureCountBeforePendingRequest = 0;

	//prefer the newest API version, but retain compatibility with older RenderDoc installations that still expose one of these supported interfaces.
	static const std::array<RENDERDOC_Version, 7> supportedApiVersions =
	{
		eRENDERDOC_API_Version_1_7_0,
		eRENDERDOC_API_Version_1_6_0,
		eRENDERDOC_API_Version_1_5_0,
		eRENDERDOC_API_Version_1_4_2,
		eRENDERDOC_API_Version_1_2_0,
		eRENDERDOC_API_Version_1_1_2,
		eRENDERDOC_API_Version_1_0_0,
	};

	void AddLibraryCandidate(std::vector<std::string>& candidates, std::string candidate)
	{
		if (candidate.empty())
			return;

		//callers may provide either a RenderDoc directory or the DLL itself.
		//mormalize directories to the DLL path before de-duplicating candidates.
		if (!StringHelper::EndsWithIgnoreCase(candidate, ".dll"))
			candidate = ShaderInjectorIO::JoinPath(candidate, "renderdoc.dll");

		const auto existingCandidate = std::find_if(candidates.begin(), candidates.end(), [&](const std::string& existingPath)
		{
			return ShaderInjectorIO::PathsEqual(existingPath, candidate);
		});

		if (existingCandidate == candidates.end())
			candidates.push_back(std::move(candidate));
	}

	std::vector<std::string> CollectInstalledLibraryCandidates()
	{
		std::vector<std::string> candidates;

		//environment overrides support portable RenderDoc installations and Wine prefixes.
		AddLibraryCandidate(candidates, ShaderInjectorIO::GetEnvironmentVariable("SHADER_INJECTOR_RENDERDOC_PATH"));
		AddLibraryCandidate(candidates, ShaderInjectorIO::GetEnvironmentVariable("RENDERDOC_PATH"));

		const std::string renderDocOpenCommandKey = "SOFTWARE\\Classes\\RenderDoc.RDCCapture.1\\shell\\open\\command";
		const std::string machineRegisteredExecutable = StringHelper::ExecutablePathFromCommandLine(ShaderInjectorIO::ReadRegistryString(ShaderInjectorIO::RegistryHive::LocalMachine, renderDocOpenCommandKey));
		const std::string userRegisteredExecutable = StringHelper::ExecutablePathFromCommandLine(ShaderInjectorIO::ReadRegistryString(ShaderInjectorIO::RegistryHive::CurrentUser, renderDocOpenCommandKey));
		AddLibraryCandidate(candidates, ShaderInjectorIO::DirectoryFromPath(machineRegisteredExecutable));
		AddLibraryCandidate(candidates, ShaderInjectorIO::DirectoryFromPath(userRegisteredExecutable));

		const std::string programW6432 = ShaderInjectorIO::GetEnvironmentVariable("ProgramW6432");
		const std::string programFiles = ShaderInjectorIO::GetEnvironmentVariable("ProgramFiles");

		if (!programW6432.empty())
			AddLibraryCandidate(candidates, ShaderInjectorIO::JoinPath(programW6432, "RenderDoc"));

		if (!programFiles.empty())
			AddLibraryCandidate(candidates, ShaderInjectorIO::JoinPath(programFiles, "RenderDoc"));

		AddLibraryCandidate(candidates, "C:\\Program Files\\RenderDoc\\renderdoc.dll");
		return candidates;
	}

	bool TryLoadInstalledModuleLocked()
	{
		bool installationFound = false;
		gRenderDocLoadError = ERROR_SUCCESS;

		for (const std::string& candidate : CollectInstalledLibraryCandidates())
		{
			if (!ShaderInjectorIO::FileExists(candidate))
				continue;

			installationFound = true;
			const std::wstring wideCandidate = StringHelper::Utf8ToWide(candidate);
			gRenderDocModule = wideCandidate.empty() ? nullptr : LoadLibraryW(wideCandidate.c_str());

			if (gRenderDocModule)
			{
				gRenderDocLoadedByInjector = true;
				gRenderDocLibraryPath = candidate;
				return true;
			}

			gRenderDocLoadError = GetLastError();
		}

		gAvailability = installationFound ? RenderDocAvailability::ModuleLoadFailed : RenderDocAvailability::InstallationNotFound;
		return false;
	}

	bool LaunchReplayUiLocked()
	{
		if (!gRenderDocApi)
			return false;

		//LaunchReplayUI is idempotent from the injector's perspective. 
		//reuse an existing target-control connection instead of starting another UI process.
		if (gRenderDocApi->IsTargetControlConnected() != 0)
			return true;

		gReplayUiProcessId = gRenderDocApi->LaunchReplayUI(1, nullptr);
		return gReplayUiProcessId != 0;
	}

	void ResetApiStateLocked()
	{
		gRenderDocApi = nullptr;
		gApiMajorVersion = 0;
		gApiMinorVersion = 0;
		gApiPatchVersion = 0;
	}

	bool ResolveRenderDocModuleLocked(bool loadInstalledModule)
	{
		//GetModuleHandle detects RenderDoc when it was injected by the launcher, another overlay, or the user. 
		//loading from disk is only allowed when the caller explicitly requests attachment.
		gRenderDocModule = GetModuleHandleW(L"renderdoc.dll");

		if (!gRenderDocModule && !loadInstalledModule)
		{
			gAvailability = RenderDocAvailability::NotAttached;
			return false;
		}

		if (!gRenderDocModule && !TryLoadInstalledModuleLocked())
			return false;

		if (gRenderDocLibraryPath.empty())
			gRenderDocLibraryPath = ShaderInjectorIO::GetLoadedModulePath("renderdoc.dll");

		return true;
	}

	bool ResolveRenderDocApiLocked()
	{
		const auto getRenderDocApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(gRenderDocModule, "RENDERDOC_GetAPI"));

		if (!getRenderDocApi)
		{
			gAvailability = RenderDocAvailability::ApiEntryPointMissing;
			return false;
		}

		for (RENDERDOC_Version apiVersion : supportedApiVersions)
		{
			void* api = nullptr;

			if (getRenderDocApi(apiVersion, &api) == 1 && api)
			{
				gRenderDocApi = static_cast<RENDERDOC_API_1_6_0*>(api);
				break;
			}
		}

		if (!gRenderDocApi)
		{
			gAvailability = RenderDocAvailability::ApiVersionUnsupported;
			return false;
		}

		gRenderDocApi->GetAPIVersion(&gApiMajorVersion, &gApiMinorVersion, &gApiPatchVersion);
		return true;
	}

	bool DiscoverApiLocked(bool loadInstalledModule)
	{
		ResetApiStateLocked();

		if (!Globals::gRenderDocIntegrationEnabled)
		{
			gAvailability = RenderDocAvailability::Disabled;
			return false;
		}

		if (!ResolveRenderDocModuleLocked(loadInstalledModule) || !ResolveRenderDocApiLocked())
			return false;

		gAvailability = RenderDocAvailability::Ready;
		return true;
	}

	void LogAvailabilityLocked(const char* functionName)
	{
		const std::string message = std::string("RenderDocIntegration->") + functionName + ": " + RenderDocAvailabilityText(gAvailability);

		if (gAvailability == RenderDocAvailability::Ready)
		{
			ShaderInjectorIO::WriteToLogFileSuccess(
				message + " API=" + std::to_string(gApiMajorVersion) + "." +
				std::to_string(gApiMinorVersion) + "." + std::to_string(gApiPatchVersion) +
				" library=\"" + gRenderDocLibraryPath + "\"" +
				" loadedByInjector=" + std::to_string(gRenderDocLoadedByInjector));
		}
		else if (gAvailability == RenderDocAvailability::ModuleLoadFailed)
		{
			ShaderInjectorIO::WriteToLogFileError(message + " win32Error=" + std::to_string(gRenderDocLoadError));
		}
		else
		{
			ShaderInjectorIO::WriteToLogFile(message);
		}
	}

	//public lifecycle and status API.
	void Initialize()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		const bool renderDocAlreadyInjected = GetModuleHandleW(L"renderdoc.dll") != nullptr;
		DiscoverApiLocked(Globals::gRenderDocAutoAttachEnabled);

		if (Globals::gRenderDocAutoAttachEnabled && gAvailability == RenderDocAvailability::Ready && !renderDocAlreadyInjected && LaunchReplayUiLocked())
			ShaderInjectorIO::WriteToLogFileSuccess("RenderDocIntegration->Initialize: launched connected RenderDoc UI pid=" + std::to_string(gReplayUiProcessId));
		else if (Globals::gRenderDocIntegrationEnabled && !Globals::gRenderDocAutoAttachEnabled && !renderDocAlreadyInjected)
			ShaderInjectorIO::WriteToLogFile("RenderDocIntegration->Initialize: automatic attachment disabled; use the RenderDoc developer tab to attach when needed");

		LogAvailabilityLocked("Initialize");
	}

	void Refresh()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		//refresh is observational. 
		//loading the capture layer is reserved for the explicit attach action so a status check cannot alter the live D3D12 device.
		DiscoverApiLocked(false);

		LogAvailabilityLocked("Refresh");
	}

	bool IsAvailable()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gAvailability == RenderDocAvailability::Ready && gRenderDocApi;
	}

	bool IsFrameCapturing()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gRenderDocApi && gRenderDocApi->IsFrameCapturing() != 0;
	}

	bool IsTargetControlConnected()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gRenderDocApi && gRenderDocApi->IsTargetControlConnected() != 0;
	}

	bool WasLoadedByInjector()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gRenderDocLoadedByInjector;
	}

	//queue a capture request for RenderDoc.
	//the request is completed asynchronously by the capture layer, so PollCaptureStatus observes its progress later.
	RenderDocCaptureRequestResult RequestFrameCapture(void* d3d12Device, void* windowHandle)
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		if (!Globals::gRenderDocIntegrationEnabled)
			return RenderDocCaptureRequestResult::Disabled;

		if (!gRenderDocApi && !DiscoverApiLocked(true))
			return RenderDocCaptureRequestResult::Unavailable;

		if (gRenderDocApi->IsFrameCapturing() != 0)
			return RenderDocCaptureRequestResult::AlreadyCapturing;

		//RenderDoc owns the API root handles used by its capture layer.
		//a raw device pointer borrowed from a swap chain is not guaranteed to be the same registered handle when RenderDoc, Proton, OptiScaler, or another wrapper is present.
		//the RenderDoc overlay has already selected the active game API/window pair, so keep that proven selection instead of overriding it with an injector pointer.
		(void)d3d12Device;
		ShaderInjectorIO::WriteToLogFile(StringHelper::Format("RenderDocIntegration->RequestFrameCapture: using RenderDoc active target window=%p", windowHandle));

		gCaptureCountBeforePendingRequest = gRenderDocApi->GetNumCaptures();
		gCaptureRequestPending = true;
		gCaptureStartedForPendingRequest = false;
		gCaptureRequestSequence.fetch_add(1, std::memory_order_release);
		gRenderDocApi->TriggerCapture();
		return RenderDocCaptureRequestResult::Queued;
	}

	void PollCaptureStatus()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		if (!gCaptureRequestPending || !gRenderDocApi)
			return;

		const bool captureActive = gRenderDocApi->IsFrameCapturing() != 0;
		if (captureActive && !gCaptureStartedForPendingRequest)
		{
			gCaptureStartedForPendingRequest = true;
			ShaderInjectorIO::WriteToLogFileSuccess("RenderDocIntegration->PollCaptureStatus: requested frame capture is active");
		}

		const uint32_t captureCount = gRenderDocApi->GetNumCaptures();
		if (captureCount <= gCaptureCountBeforePendingRequest)
			return;

		gCaptureRequestPending = false;
		ShaderInjectorIO::WriteToLogFileSuccess(StringHelper::Format(
			"RenderDocIntegration->PollCaptureStatus: capture completed count=%u passObservedActive=%u",
			captureCount,
			gCaptureStartedForPendingRequest ? 1u : 0u));
	}

	//this monotonically increasing value lets render-pass code detect a new capture request without depending on the timing of RenderDoc's asynchronous callbacks.
	uint64_t GetCaptureRequestSequence()
	{
		return gCaptureRequestSequence.load(std::memory_order_acquire);
	}

	//replay UI control is kept separate from frame capture so the UI can attach RenderDoc without forcing a capture request.
	RenderDocReplayUIRequestResult ConnectReplayUI()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		if (!Globals::gRenderDocIntegrationEnabled)
			return RenderDocReplayUIRequestResult::Disabled;

		if (!gRenderDocApi && !DiscoverApiLocked(true))
			return RenderDocReplayUIRequestResult::Unavailable;

		if (gRenderDocApi->IsTargetControlConnected() != 0)
			return RenderDocReplayUIRequestResult::AlreadyConnected;

		return LaunchReplayUiLocked() ? RenderDocReplayUIRequestResult::Launched : RenderDocReplayUIRequestResult::LaunchFailed;
	}

	//status accessors return snapshots while holding the same mutex used by lifecycle and capture operations. 
	//this keeps the GUI from observing partially updated state.
	std::string GetStatusText()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return RenderDocAvailabilityText(gAvailability);
	}

	std::string GetApiVersionText()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		if (!gRenderDocApi)
			return "Unavailable";

		return std::to_string(gApiMajorVersion) + "." + std::to_string(gApiMinorVersion) + "." + std::to_string(gApiPatchVersion);
	}

	std::string GetLibraryPath()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gRenderDocLibraryPath;
	}

	uint32_t GetCaptureCount()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gRenderDocApi ? gRenderDocApi->GetNumCaptures() : 0;
	}

	uint32_t GetReplayUiProcessId()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gReplayUiProcessId;
	}

	std::string GetLatestCapturePath()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		if (!gRenderDocApi)
			return {};

		const uint32_t captureCount = gRenderDocApi->GetNumCaptures();

		if (captureCount == 0)
			return {};

		uint32_t pathLength = 0;

		if (gRenderDocApi->GetCapture(captureCount - 1, nullptr, &pathLength, nullptr) == 0 || pathLength == 0)
			return {};

		std::vector<char> capturePath(pathLength + 1, '\0');
		uint32_t capturePathCapacity = static_cast<uint32_t>(capturePath.size());

		if (gRenderDocApi->GetCapture(captureCount - 1, capturePath.data(), &capturePathCapacity, nullptr) == 0)
			return {};

		capturePath.back() = '\0';

		return capturePath.data();
	}
}
