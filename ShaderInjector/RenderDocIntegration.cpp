#include "RenderDocIntegration.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

#include "Globals.h"
#include "ProcessRunner.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"
#include "renderdoc_app.h"

namespace RenderDocIntegration
{
	namespace
	{
		enum class Availability
		{
			Disabled,
			NotAttached,
			InstallationNotFound,
			ModuleLoadFailed,
			ApiEntryPointMissing,
			ApiVersionUnsupported,
			Ready,
		};

		std::mutex gRenderDocMutex;
		HMODULE gRenderDocModule = nullptr;
		RENDERDOC_API_1_6_0* gRenderDocApi = nullptr;
		Availability gAvailability = Availability::InstallationNotFound;
		bool gRenderDocLoadedByInjector = false;
		std::string gRenderDocLibraryPath;
		DWORD gRenderDocLoadError = ERROR_SUCCESS;
		uint32_t gReplayUiProcessId = 0;
		int gApiMajorVersion = 0;
		int gApiMinorVersion = 0;
		int gApiPatchVersion = 0;

		const std::array<RENDERDOC_Version, 7> supportedApiVersions =
		{
			eRENDERDOC_API_Version_1_7_0,
			eRENDERDOC_API_Version_1_6_0,
			eRENDERDOC_API_Version_1_5_0,
			eRENDERDOC_API_Version_1_4_2,
			eRENDERDOC_API_Version_1_2_0,
			eRENDERDOC_API_Version_1_1_2,
			eRENDERDOC_API_Version_1_0_0,
		};

		const char* AvailabilityText(Availability availability)
		{
			switch (availability)
			{
				case Availability::Disabled: return "Disabled";
				case Availability::NotAttached: return "Not attached";
				case Availability::InstallationNotFound: return "RenderDoc installation was not found";
				case Availability::ModuleLoadFailed: return "RenderDoc library failed to load";
				case Availability::ApiEntryPointMissing: return "RENDERDOC_GetAPI is unavailable";
				case Availability::ApiVersionUnsupported: return "RenderDoc API version is unsupported";
				case Availability::Ready: return "Ready";
				default: return "Unavailable";
			}
		}

		void AddLibraryCandidate(std::vector<std::string>& candidates, std::string candidate)
		{
			if (candidate.empty())
				return;

			if (!StringHelper::EndsWithIgnoreCase(candidate, ".dll"))
				candidate = ShaderInjectorIO::JoinPath(candidate, "renderdoc.dll");

			const auto existingCandidate = std::find_if(candidates.begin(), candidates.end(), [&](const std::string& existingPath)
			{
				return ShaderInjectorIO::PathsEqual(existingPath, candidate);
			});

			if (existingCandidate == candidates.end())
				candidates.push_back(std::move(candidate));
		}

		void AddRunningRenderDocCandidates(std::vector<std::string>& candidates)
		{
			for (const std::string& executablePath : ProcessRunner::FindProcessExecutablePaths("qrenderdoc.exe"))
			{
				AddLibraryCandidate(candidates, ShaderInjectorIO::DirectoryFromPath(executablePath));
			}
		}

		std::vector<std::string> CollectInstalledLibraryCandidates()
		{
			std::vector<std::string> candidates;

			// Explicit environment overrides support portable RenderDoc installs and Wine prefixes.
			AddLibraryCandidate(candidates, ProcessRunner::GetEnvironmentVariable("SHADER_INJECTOR_RENDERDOC_PATH"));
			AddLibraryCandidate(candidates, ProcessRunner::GetEnvironmentVariable("RENDERDOC_PATH"));

			// Prefer the capture library beside the exact RenderDoc UI the user already opened.
			AddRunningRenderDocCandidates(candidates);

			const std::string renderDocOpenCommandKey = "SOFTWARE\\Classes\\RenderDoc.RDCCapture.1\\shell\\open\\command";
			const std::string machineRegisteredExecutable = StringHelper::ExecutablePathFromCommandLine(ShaderInjectorIO::ReadRegistryString(ShaderInjectorIO::RegistryHive::LocalMachine, renderDocOpenCommandKey));
			const std::string userRegisteredExecutable = StringHelper::ExecutablePathFromCommandLine(ShaderInjectorIO::ReadRegistryString(ShaderInjectorIO::RegistryHive::CurrentUser, renderDocOpenCommandKey));
			AddLibraryCandidate(candidates, ShaderInjectorIO::DirectoryFromPath(machineRegisteredExecutable));
			AddLibraryCandidate(candidates, ShaderInjectorIO::DirectoryFromPath(userRegisteredExecutable));

			const std::string programW6432 = ProcessRunner::GetEnvironmentVariable("ProgramW6432");
			const std::string programFiles = ProcessRunner::GetEnvironmentVariable("ProgramFiles");

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

			gAvailability = installationFound ? Availability::ModuleLoadFailed : Availability::InstallationNotFound;
			return false;
		}

		bool LaunchReplayUiLocked()
		{
			if (!gRenderDocApi)
				return false;

			if (gRenderDocApi->IsTargetControlConnected() != 0)
				return true;

			gReplayUiProcessId = gRenderDocApi->LaunchReplayUI(1, nullptr);
			return gReplayUiProcessId != 0;
		}

		bool DiscoverApiLocked(bool loadInstalledModule)
		{
			gRenderDocApi = nullptr;
			gApiMajorVersion = 0;
			gApiMinorVersion = 0;
			gApiPatchVersion = 0;

			if (!Globals::gRenderDocIntegrationEnabled)
			{
				gAvailability = Availability::Disabled;
				return false;
			}

			gRenderDocModule = GetModuleHandleW(L"renderdoc.dll");

			if (!gRenderDocModule && !loadInstalledModule)
			{
				gAvailability = Availability::NotAttached;
				return false;
			}

			if (!gRenderDocModule && (!loadInstalledModule || !TryLoadInstalledModuleLocked()))
				return false;

			if (gRenderDocLibraryPath.empty())
				gRenderDocLibraryPath = ProcessRunner::GetLoadedModulePath("renderdoc.dll");

			const auto getRenderDocApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(gRenderDocModule, "RENDERDOC_GetAPI"));

			if (!getRenderDocApi)
			{
				gAvailability = Availability::ApiEntryPointMissing;
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
				gAvailability = Availability::ApiVersionUnsupported;
				return false;
			}

			gRenderDocApi->GetAPIVersion(&gApiMajorVersion, &gApiMinorVersion, &gApiPatchVersion);
			gAvailability = Availability::Ready;
			return true;
		}

		void LogAvailabilityLocked(const char* functionName)
		{
			const std::string message = std::string("RenderDocIntegration->") + functionName + ": " + AvailabilityText(gAvailability);

			if (gAvailability == Availability::Ready)
			{
				ShaderInjectorIO::WriteToLogFileSuccess(
					message + " API=" + std::to_string(gApiMajorVersion) + "." +
					std::to_string(gApiMinorVersion) + "." + std::to_string(gApiPatchVersion) +
					" library=\"" + gRenderDocLibraryPath + "\"" +
					" loadedByInjector=" + std::to_string(gRenderDocLoadedByInjector));
			}
			else if (gAvailability == Availability::ModuleLoadFailed)
			{
				ShaderInjectorIO::WriteToLogFileError(message + " win32Error=" + std::to_string(gRenderDocLoadError));
			}
			else
			{
				ShaderInjectorIO::WriteToLogFile(message);
			}
		}
	}

	void Initialize()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		const bool renderDocAlreadyInjected = GetModuleHandleW(L"renderdoc.dll") != nullptr;
		DiscoverApiLocked(Globals::gRenderDocAutoAttachEnabled);

		if (Globals::gRenderDocAutoAttachEnabled && gAvailability == Availability::Ready && !renderDocAlreadyInjected && LaunchReplayUiLocked())
			ShaderInjectorIO::WriteToLogFileSuccess("RenderDocIntegration->Initialize: launched connected RenderDoc UI pid=" + std::to_string(gReplayUiProcessId));
		else if (Globals::gRenderDocIntegrationEnabled && !Globals::gRenderDocAutoAttachEnabled && !renderDocAlreadyInjected)
			ShaderInjectorIO::WriteToLogFile("RenderDocIntegration->Initialize: automatic attachment disabled; use the RenderDoc developer tab to attach when needed");

		LogAvailabilityLocked("Initialize");
	}

	void Refresh()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		// Refresh is observational. Loading the capture layer is reserved for the explicit
		// Attach action so a status check cannot alter the live D3D12 device unexpectedly.
		DiscoverApiLocked(false);

		LogAvailabilityLocked("Refresh");
	}

	bool IsAvailable()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return gAvailability == Availability::Ready && gRenderDocApi;
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

	CaptureRequestResult RequestFrameCapture(void* d3d12Device, void* windowHandle)
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		if (!Globals::gRenderDocIntegrationEnabled)
			return CaptureRequestResult::Disabled;

		if (!gRenderDocApi && !DiscoverApiLocked(true))
			return CaptureRequestResult::Unavailable;

		if (gRenderDocApi->IsFrameCapturing() != 0)
			return CaptureRequestResult::AlreadyCapturing;

		if (d3d12Device && windowHandle)
			gRenderDocApi->SetActiveWindow(d3d12Device, windowHandle);

		gRenderDocApi->TriggerCapture();

		return CaptureRequestResult::Queued;
	}

	ReplayUiRequestResult ConnectReplayUi()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);

		if (!Globals::gRenderDocIntegrationEnabled)
			return ReplayUiRequestResult::Disabled;

		if (!gRenderDocApi && !DiscoverApiLocked(true))
			return ReplayUiRequestResult::Unavailable;

		if (gRenderDocApi->IsTargetControlConnected() != 0)
			return ReplayUiRequestResult::AlreadyConnected;

		return LaunchReplayUiLocked() ? ReplayUiRequestResult::Launched : ReplayUiRequestResult::LaunchFailed;
	}

	std::string GetStatusText()
	{
		std::lock_guard<std::mutex> lock(gRenderDocMutex);
		return AvailabilityText(gAvailability);
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
