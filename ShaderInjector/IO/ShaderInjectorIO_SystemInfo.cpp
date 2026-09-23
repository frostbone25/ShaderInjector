#include "ShaderInjectorIO.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <intrin.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Enum/EnumStrings.h"
#include "StringHelper.h"

#pragma comment(lib, "Version.lib")

namespace ShaderInjectorIO
{
	//keep one-time log guards and formatting helpers private to this translation unit.
	static std::once_flag processAndSystemInfoLogFlag;
	static std::once_flag d3d12DeviceInfoLogFlag;

	static std::string VersionDWORDToString(DWORD mostSignificantDWORD, DWORD leastSignificantDWORD)
	{
		//split each DWORD into its high and low words to print the four-part file version.
		std::ostringstream versionTextStream;
		versionTextStream
			<< HIWORD(mostSignificantDWORD) << "."
			<< LOWORD(mostSignificantDWORD) << "."
			<< HIWORD(leastSignificantDWORD) << "."
			<< LOWORD(leastSignificantDWORD);
		return versionTextStream.str();
	}

	static std::string LUIDToString(LUID adapterLUID)
	{
		std::ostringstream adapterLUIDTextStream;
		adapterLUIDTextStream << StringHelper::FormatUnsignedHex(static_cast<uint32_t>(adapterLUID.HighPart), 8) << ":" << StringHelper::FormatUnsignedHex(adapterLUID.LowPart, 8);
		return adapterLUIDTextStream.str();
	}

	static bool SameLUID(LUID leftLUID, LUID rightLUID)
	{
		return leftLUID.HighPart == rightLUID.HighPart && leftLUID.LowPart == rightLUID.LowPart;
	}

	static std::string GetCPUBrandString()
	{
		//the three extended CPUID leaves contain the 48-character processor brand.
		std::array<int, 4> cpuRegisters{};
		__cpuid(cpuRegisters.data(), 0x80000000);

		const unsigned int maximumExtendedLeaf = static_cast<unsigned int>(cpuRegisters[0]);

		if (maximumExtendedLeaf < 0x80000004)
			return "Unknown";

		char cpuBrandCharacters[49]{};

		for (int brandLeafIndex = 0; brandLeafIndex < 3; ++brandLeafIndex)
		{
			std::array<int, 4> cpuBrandRegisters{};
			__cpuid(cpuBrandRegisters.data(), 0x80000002 + brandLeafIndex);
			std::memcpy(cpuBrandCharacters + brandLeafIndex * 16, cpuBrandRegisters.data(), 16);
		}

		std::string cpuBrandName = cpuBrandCharacters;

		while (!cpuBrandName.empty() && cpuBrandName.front() == ' ')
			cpuBrandName.erase(cpuBrandName.begin());

		while (!cpuBrandName.empty() && cpuBrandName.back() == ' ')
			cpuBrandName.pop_back();

		if (cpuBrandName.empty())
			return "Unknown";

		return cpuBrandName;
	}

	static std::string GetWindowsVersionString()
	{
		//use RtlGetVersion directly so compatibility settings do not mask the installed windows version.
		using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

		HMODULE ntdllModule = GetModuleHandleW(L"ntdll.dll");
		RtlGetVersionFunction windowsVersionFunction = nullptr;

		if (ntdllModule)
			windowsVersionFunction = reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdllModule, "RtlGetVersion"));

		RTL_OSVERSIONINFOW versionInfo{};
		versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);

		if (windowsVersionFunction && windowsVersionFunction(&versionInfo) == 0)
		{
			std::ostringstream windowsVersionTextStream;
			windowsVersionTextStream << versionInfo.dwMajorVersion << "." << versionInfo.dwMinorVersion << "." << versionInfo.dwBuildNumber;

			if (versionInfo.szCSDVersion[0])
				windowsVersionTextStream << " " << StringHelper::WideToUtf8(versionInfo.szCSDVersion);

			return windowsVersionTextStream.str();
		}

		return "Unknown";
	}

	static std::string GetVersionStringValue(
		const std::vector<BYTE>& versionData,
		WORD language,
		WORD codePage,
		const wchar_t* versionStringKey)
	{
		std::wostringstream versionStringSubBlock;
		versionStringSubBlock
			<< L"\\StringFileInfo\\"
			<< std::uppercase << std::hex << std::setw(4) << std::setfill(L'0') << language
			<< std::setw(4) << std::setfill(L'0') << codePage
			<< L"\\" << versionStringKey;

		LPVOID versionStringBuffer = nullptr;
		UINT versionStringBufferBytes = 0;

		if (!VerQueryValueW(versionData.data(), versionStringSubBlock.str().c_str(), &versionStringBuffer, &versionStringBufferBytes) || !versionStringBuffer || versionStringBufferBytes == 0)
			return "";

		return StringHelper::WideToUtf8(static_cast<const wchar_t*>(versionStringBuffer));
	}

	static void LogExecutableMetadata()
	{
		//read windows version resources so logs identify the executable build being injected.
		const std::wstring executablePath = StringHelper::Utf8ToWide(GetCurrentExecutablePath());

		if (executablePath.empty())
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Executable: path unavailable");
			return;
		}

		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Executable: Path = " + StringHelper::WideToUtf8(executablePath));

		DWORD unusedVersionHandle = 0;
		const DWORD versionDataBytes = GetFileVersionInfoSizeW(executablePath.c_str(), &unusedVersionHandle);

		if (versionDataBytes == 0)
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Executable: no version metadata found");
			return;
		}

		//load the resource once, then query fixed and localized fields from the same byte buffer.
		std::vector<BYTE> versionData(versionDataBytes);

		if (!GetFileVersionInfoW(executablePath.c_str(), 0, versionDataBytes, versionData.data()))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Executable: failed to read version metadata");
			return;
		}

		VS_FIXEDFILEINFO* fixedFileInfo = nullptr;
		UINT fixedFileInfoBytes = 0;

		if (VerQueryValueW(versionData.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedFileInfo), &fixedFileInfoBytes) &&
			fixedFileInfo &&
			fixedFileInfoBytes >= sizeof(VS_FIXEDFILEINFO) &&
			fixedFileInfo->dwSignature == 0xFEEF04BD)
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Executable: FileVersionFixed = " + VersionDWORDToString(fixedFileInfo->dwFileVersionMS, fixedFileInfo->dwFileVersionLS));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Executable: ProductVersionFixed = " + VersionDWORDToString(fixedFileInfo->dwProductVersionMS, fixedFileInfo->dwProductVersionLS));
		}

		//each translation is a language/code-page pair used to find localized strings.
		WORD* translationWords = nullptr;
		UINT translationBytes = 0;
		std::vector<std::pair<WORD, WORD>> translationList;

		if (VerQueryValueW(versionData.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translationWords), &translationBytes) &&
			translationWords &&
			translationBytes >= sizeof(WORD) * 2)
		{
			const UINT translationCount = translationBytes / (sizeof(WORD) * 2);

			for (UINT translationIndex = 0; translationIndex < translationCount; ++translationIndex)
				translationList.emplace_back(translationWords[translationIndex * 2], translationWords[translationIndex * 2 + 1]);
		}
		else
		{
			translationList.push_back({ 0x0409, 1200 });
		}

		//try each translation because one executable can store these labels in multiple languages.
		const std::array<const wchar_t*, 7> versionStringKeys =
		{
			L"CompanyName",
			L"FileDescription",
			L"FileVersion",
			L"InternalName",
			L"OriginalFilename",
			L"ProductName",
			L"ProductVersion",
		};

		for (const wchar_t* versionStringKey : versionStringKeys)
		{
			std::string versionStringValue;

			for (const std::pair<WORD, WORD>& translation : translationList)
			{
				versionStringValue = GetVersionStringValue(versionData, translation.first, translation.second, versionStringKey);

				if (!versionStringValue.empty())
					break;
			}

			if (!versionStringValue.empty())
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Executable: " + StringHelper::WideToUtf8(versionStringKey) + " = " + versionStringValue);
		}
	}

	static void LogSystemInfo()
	{
		//capture one system snapshot so hardware and memory details share the same startup timestamp.
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: OSVersion=" + GetWindowsVersionString());

		SYSTEM_INFO systemInformation{};
		GetNativeSystemInfo(&systemInformation);

		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: CPU = " + GetCPUBrandString());
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: CPUArchitecture = " + GetProcessorArchitectureName(systemInformation.wProcessorArchitecture));
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: LogicalProcessors = " + std::to_string(systemInformation.dwNumberOfProcessors));
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: ProcessorGroups = " + std::to_string(GetActiveProcessorGroupCount()));
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: ActiveProcessorsAllGroups = " + std::to_string(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: PageSize = " + std::to_string(systemInformation.dwPageSize));
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->System: AllocationGranularity = " + std::to_string(systemInformation.dwAllocationGranularity));

		//GlobalMemoryStatusEx needs its size set before it fills the available physical and virtual memory values.
		MEMORYSTATUSEX systemMemoryStatus{};
		systemMemoryStatus.dwLength = sizeof(systemMemoryStatus);

		if (GlobalMemoryStatusEx(&systemMemoryStatus))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->SystemMemory: LoadPercent = " + std::to_string(systemMemoryStatus.dwMemoryLoad));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->SystemMemory: TotalPhysical = " + StringHelper::FormatBytesAsGiB(systemMemoryStatus.ullTotalPhys));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->SystemMemory: AvailablePhysical = " + StringHelper::FormatBytesAsGiB(systemMemoryStatus.ullAvailPhys));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->SystemMemory: TotalPageFile = " + StringHelper::FormatBytesAsGiB(systemMemoryStatus.ullTotalPageFile));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->SystemMemory: AvailablePageFile = " + StringHelper::FormatBytesAsGiB(systemMemoryStatus.ullAvailPageFile));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->SystemMemory: TotalVirtual = " + StringHelper::FormatBytesAsGiB(systemMemoryStatus.ullTotalVirtual));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->SystemMemory: AvailableVirtual = " + StringHelper::FormatBytesAsGiB(systemMemoryStatus.ullAvailVirtual));
		}

		const int primaryDisplayWidth = GetSystemMetrics(SM_CXSCREEN);
		const int primaryDisplayHeight = GetSystemMetrics(SM_CYSCREEN);
		const int virtualDesktopX = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int virtualDesktopY = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int virtualDesktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int virtualDesktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		const int connectedMonitorCount = GetSystemMetrics(SM_CMONITORS);

		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Display: PrimaryResolution = " + std::to_string(primaryDisplayWidth) + "x" + std::to_string(primaryDisplayHeight));
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Display: VirtualDesktop = " + std::to_string(virtualDesktopWidth) + "x" + std::to_string(virtualDesktopHeight) + " at " + std::to_string(virtualDesktopX) + "," + std::to_string(virtualDesktopY));
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->Display: MonitorCount = " + std::to_string(connectedMonitorCount));
	}

	static void LogAdapterInfo(ID3D12Device* d3d12Device)
	{
		//match the device's adapter LUID so multi-GPU systems report the active card.
		const LUID deviceLUID = d3d12Device->GetAdapterLuid();
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Device: AdapterLuid = " + LUIDToString(deviceLUID));

		Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Device: CreateDXGIFactory1 failed; adapter details unavailable");
			return;
		}

		Microsoft::WRL::ComPtr<IDXGIAdapter1> matchingDXGIAdapter;
		for (UINT adapterIndex = 0;; ++adapterIndex)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> dxgiAdapter;
			if (factory->EnumAdapters1(adapterIndex, &dxgiAdapter) == DXGI_ERROR_NOT_FOUND)
				break;

			DXGI_ADAPTER_DESC1 adapterDescription{};
			if (FAILED(dxgiAdapter->GetDesc1(&adapterDescription)))
				continue;

			if (SameLUID(adapterDescription.AdapterLuid, deviceLUID))
			{
				matchingDXGIAdapter = dxgiAdapter;
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: AdapterIndex = " + std::to_string(adapterIndex));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: Description = " + StringHelper::WideToUtf8(adapterDescription.Description));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: VendorId = " + StringHelper::FormatUnsignedHex(adapterDescription.VendorId, 4));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: DeviceId = " + StringHelper::FormatUnsignedHex(adapterDescription.DeviceId, 4));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: SubSysId = " + StringHelper::FormatUnsignedHex(adapterDescription.SubSysId, 8));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: Revision = " + std::to_string(adapterDescription.Revision));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: DedicatedVideoMemory = " + StringHelper::FormatBytesAsGiB(adapterDescription.DedicatedVideoMemory));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: DedicatedSystemMemory = " + StringHelper::FormatBytesAsGiB(adapterDescription.DedicatedSystemMemory));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: SharedSystemMemory = " + StringHelper::FormatBytesAsGiB(adapterDescription.SharedSystemMemory));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: Flags = " + StringHelper::FormatUnsignedHex(adapterDescription.Flags, 8));
				break;
			}
		}

		if (!matchingDXGIAdapter)
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPU: matching DXGI adapter not found");
			return;
		}

		//query the newer adapter interface only when it exposes the per-segment memory budgets.
		Microsoft::WRL::ComPtr<IDXGIAdapter3> adapterWithMemoryBudget;
		if (SUCCEEDED(matchingDXGIAdapter.As(&adapterWithMemoryBudget)))
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO localMemoryInfo{};
			if (SUCCEEDED(adapterWithMemoryBudget->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localMemoryInfo)))
			{
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: LocalBudget = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.Budget));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: LocalCurrentUsage = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.CurrentUsage));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: LocalAvailableForReservation = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.AvailableForReservation));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: LocalCurrentReservation = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.CurrentReservation));
			}

			DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalMemoryInfo{};
			if (SUCCEEDED(adapterWithMemoryBudget->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalMemoryInfo)))
			{
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: NonLocalBudget = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.Budget));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: NonLocalCurrentUsage = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.CurrentUsage));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: NonLocalAvailableForReservation = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.AvailableForReservation));
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->GPUMemory: NonLocalCurrentReservation = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.CurrentReservation));
			}
		}
	}

	static void LogD3D12FeatureSupport(ID3D12Device* d3d12Device)
	{
		//ask D3D12 for each feature family separately because older drivers may support only some of them.
		std::array<D3D_FEATURE_LEVEL, 5> requestedFeatureLevels =
		{
			D3D_FEATURE_LEVEL_12_2,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		D3D12_FEATURE_DATA_FEATURE_LEVELS requestedFeatureLevelData{};
		requestedFeatureLevelData.NumFeatureLevels = static_cast<UINT>(requestedFeatureLevels.size());
		requestedFeatureLevelData.pFeatureLevelsRequested = requestedFeatureLevels.data();

		if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &requestedFeatureLevelData, sizeof(requestedFeatureLevelData))))
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: MaxFeatureLevel = " + D3DFeatureLevelToString(requestedFeatureLevelData.MaxSupportedFeatureLevel));

		//the base options report shader math, resource binding, and rasterizer behavior.
		D3D12_FEATURE_DATA_D3D12_OPTIONS baseFeatureOptions{};
		if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &baseFeatureOptions, sizeof(baseFeatureOptions))))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: DoublePrecisionFloatShaderOps = " + StringHelper::BoolText(baseFeatureOptions.DoublePrecisionFloatShaderOps != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: OutputMergerLogicOp = " + StringHelper::BoolText(baseFeatureOptions.OutputMergerLogicOp != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: ResourceBindingTier = " + std::to_string(static_cast<UINT>(baseFeatureOptions.ResourceBindingTier)));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: ResourceHeapTier = " + std::to_string(static_cast<UINT>(baseFeatureOptions.ResourceHeapTier)));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: TiledResourcesTier = " + std::to_string(static_cast<UINT>(baseFeatureOptions.TiledResourcesTier)));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: ConservativeRasterizationTier = " + std::to_string(static_cast<UINT>(baseFeatureOptions.ConservativeRasterizationTier)));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: CrossNodeSharingTier = " + std::to_string(static_cast<UINT>(baseFeatureOptions.CrossNodeSharingTier)));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation = " + StringHelper::BoolText(baseFeatureOptions.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: TypedUAVLoadAdditionalFormats = " + StringHelper::BoolText(baseFeatureOptions.TypedUAVLoadAdditionalFormats != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: StandardSwizzle64KBSupported = " + StringHelper::BoolText(baseFeatureOptions.StandardSwizzle64KBSupported != FALSE));
		}

		//options1 adds wave operations used by some compute and pixel shaders.
		D3D12_FEATURE_DATA_D3D12_OPTIONS1 waveFeatureOptions{};
		if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &waveFeatureOptions, sizeof(waveFeatureOptions))))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: WaveOps = " + StringHelper::BoolText(waveFeatureOptions.WaveOps != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: WaveLaneCountMin = " + std::to_string(waveFeatureOptions.WaveLaneCountMin));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: WaveLaneCountMax = " + std::to_string(waveFeatureOptions.WaveLaneCountMax));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: TotalLaneCount = " + std::to_string(waveFeatureOptions.TotalLaneCount));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: ExpandedComputeResourceStates = " + StringHelper::BoolText(waveFeatureOptions.ExpandedComputeResourceStates != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: Int64ShaderOps = " + StringHelper::BoolText(waveFeatureOptions.Int64ShaderOps != FALSE));
		}

		//options5 reports render pass and raytracing tiers when the driver supports them.
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 raytracingFeatureOptions{};
		if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &raytracingFeatureOptions, sizeof(raytracingFeatureOptions))))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: SRVOnlyTiledResourceTier3 = " + StringHelper::BoolText(raytracingFeatureOptions.SRVOnlyTiledResourceTier3 != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: RenderPassesTier = " + std::to_string(static_cast<UINT>(raytracingFeatureOptions.RenderPassesTier)));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: RaytracingTier = " + std::to_string(static_cast<UINT>(raytracingFeatureOptions.RaytracingTier)));
		}

		//options7 covers newer mesh shader and sampler feedback support.
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 meshShaderFeatureOptions{};
		if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &meshShaderFeatureOptions, sizeof(meshShaderFeatureOptions))))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: MeshShaderTier = " + std::to_string(static_cast<UINT>(meshShaderFeatureOptions.MeshShaderTier)));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: SamplerFeedbackTier = " + std::to_string(static_cast<UINT>(meshShaderFeatureOptions.SamplerFeedbackTier)));
		}

		//architecture flags describe whether memory is shared or managed as a unified pool.
		D3D12_FEATURE_DATA_ARCHITECTURE1 architectureFeatures{};
		architectureFeatures.NodeIndex = 0;
		if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architectureFeatures, sizeof(architectureFeatures))))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: UMA = " + StringHelper::BoolText(architectureFeatures.UMA != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: CacheCoherentUMA = " + StringHelper::BoolText(architectureFeatures.CacheCoherentUMA != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: IsolatedMMU = " + StringHelper::BoolText(architectureFeatures.IsolatedMMU != FALSE));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: TileBasedRenderer = " + StringHelper::BoolText(architectureFeatures.TileBasedRenderer != FALSE));
		}

		D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpuVirtualAddressFeatures{};
		if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gpuVirtualAddressFeatures, sizeof(gpuVirtualAddressFeatures))))
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: MaxGPUVirtualAddressBitsPerResource = " + std::to_string(gpuVirtualAddressFeatures.MaxGPUVirtualAddressBitsPerResource));
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: MaxGPUVirtualAddressBitsPerProcess = " + std::to_string(gpuVirtualAddressFeatures.MaxGPUVirtualAddressBitsPerProcess));
		}

		//try the newest model first, then fall back so older D3D12 devices still report their highest supported level.
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelFeatures{};
		shaderModelFeatures.HighestShaderModel = D3D_SHADER_MODEL_6_7;

		if (FAILED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelFeatures, sizeof(shaderModelFeatures))))
		{
			shaderModelFeatures.HighestShaderModel = D3D_SHADER_MODEL_6_6;

			if (FAILED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelFeatures, sizeof(shaderModelFeatures))))
			{
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: HighestShaderModel = unavailable");
			}
			else
			{
				ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: HighestShaderModel = " + D3DShaderModelToString(shaderModelFeatures.HighestShaderModel));
			}
		}
		else
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: HighestShaderModel = " + D3DShaderModelToString(shaderModelFeatures.HighestShaderModel));
		}

		//1.1 is preferred, while 1.0 keeps the log useful on older drivers.
		D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignatureFeatures{};
		rootSignatureFeatures.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignatureFeatures, sizeof(rootSignatureFeatures))))
			rootSignatureFeatures.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

		ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12Features: HighestRootSignatureVersion = " + std::to_string(static_cast<UINT>(rootSignatureFeatures.HighestVersion)));
	}

	void LogProcessAndSystemInfo()
	{
		//emit this startup summary once even when multiple hooks request the same system details.
		std::call_once(processAndSystemInfoLogFlag, []()
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->ProcessAndSystemInfo: begin");
			LogExecutableMetadata();
			LogSystemInfo();
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->ProcessAndSystemInfo: end");
		});
	}

	void LogD3D12DeviceInfo(ID3D12Device* d3d12Device)
	{
		if (!d3d12Device)
			return;

		//only the first created D3D12 device writes adapter and capability details.
		std::call_once(d3d12DeviceInfoLogFlag, [d3d12Device]()
		{
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12DeviceInfo: begin");
			LogAdapterInfo(d3d12Device);
			LogD3D12FeatureSupport(d3d12Device);
			ShaderInjectorIO::WriteToLogFile("ShaderInjectorIO->D3D12DeviceInfo: end");
		});
	}
}
