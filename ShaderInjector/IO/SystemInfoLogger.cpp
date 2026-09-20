//SystemInfoLogger.cpp
#include "SystemInfoLogger.h"

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
#include <vector>

//custom
#include "IO/ProcessRunner.h"
#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"
#include "Enum/EnumStrings.h"

#pragma comment(lib, "Version.lib")

namespace SystemInfoLogger
{
	namespace
	{
		std::once_flag processAndSystemInfoLogFlag;
		std::once_flag d3d12DeviceInfoLogFlag;

		std::string VersionDWORDToString(DWORD mostSignificant, DWORD leastSignificant)
		{
			std::ostringstream stream;
			stream
				<< HIWORD(mostSignificant) << "."
				<< LOWORD(mostSignificant) << "."
				<< HIWORD(leastSignificant) << "."
				<< LOWORD(leastSignificant);
			return stream.str();
		}

		std::string LUIDToString(LUID luid)
		{
			std::ostringstream stream;
			stream << StringHelper::FormatUnsignedHex((uint32_t)luid.HighPart, 8) << ":" << StringHelper::FormatUnsignedHex(luid.LowPart, 8);
			return stream.str();
		}

		bool SameLUID(LUID left, LUID right)
		{
			return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
		}

		std::string GetCpuBrandString()
		{
			std::array<int, 4> registers{};
			__cpuid(registers.data(), 0x80000000);

			const unsigned int maximumExtendedLeaf = (unsigned int)registers[0];

			if (maximumExtendedLeaf < 0x80000004)
				return "Unknown";

			char brand[49]{};

			for (int leaf = 0; leaf < 3; ++leaf)
			{
				std::array<int, 4> brandRegisters{};
				__cpuid(brandRegisters.data(), 0x80000002 + leaf);
				std::memcpy(brand + leaf * 16, brandRegisters.data(), 16);
			}

			std::string result = brand;

			while (!result.empty() && result.front() == ' ')
				result.erase(result.begin());

			while (!result.empty() && result.back() == ' ')
				result.pop_back();

			return result.empty() ? "Unknown" : result;
		}

		std::string GetWindowsVersionString()
		{
			using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

			HMODULE ntdllModule = GetModuleHandleW(L"ntdll.dll");
			RtlGetVersionFunction rtlGetVersion = ntdllModule ? reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdllModule, "RtlGetVersion")) : nullptr;

			RTL_OSVERSIONINFOW versionInfo{};
			versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);

			if (rtlGetVersion && rtlGetVersion(&versionInfo) == 0)
			{
				std::ostringstream stream;
				stream << versionInfo.dwMajorVersion << "." << versionInfo.dwMinorVersion << "." << versionInfo.dwBuildNumber;

				if (versionInfo.szCSDVersion[0])
					stream << " " << StringHelper::WideToUtf8(versionInfo.szCSDVersion);

				return stream.str();
			}

			return "Unknown";
		}

		std::string GetVersionStringValue(
			const std::vector<BYTE>& versionData,
			WORD language,
			WORD codePage,
			const wchar_t* key)
		{
			std::wostringstream subBlock;
			subBlock
				<< L"\\StringFileInfo\\"
				<< std::uppercase << std::hex << std::setw(4) << std::setfill(L'0') << language
				<< std::setw(4) << std::setfill(L'0') << codePage
				<< L"\\" << key;

			LPVOID value = nullptr;
			UINT valueBytes = 0;

			if (!VerQueryValueW(versionData.data(), subBlock.str().c_str(), &value, &valueBytes) || !value || valueBytes == 0)
				return "";

			return StringHelper::WideToUtf8(static_cast<const wchar_t*>(value));
		}

		void LogExecutableMetadata()
		{
			const std::wstring executablePath = StringHelper::Utf8ToWide(ProcessRunner::GetCurrentExecutablePath());

			if (executablePath.empty())
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Executable: path unavailable");
				return;
			}

			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Executable: Path = " + StringHelper::WideToUtf8(executablePath));

			DWORD ignoredHandle = 0;
			const DWORD versionDataBytes = GetFileVersionInfoSizeW(executablePath.c_str(), &ignoredHandle);

			if (versionDataBytes == 0)
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Executable: no version metadata found");
				return;
			}

			std::vector<BYTE> versionData(versionDataBytes);

			if (!GetFileVersionInfoW(executablePath.c_str(), 0, versionDataBytes, versionData.data()))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Executable: failed to read version metadata");
				return;
			}

			VS_FIXEDFILEINFO* fixedFileInfo = nullptr;
			UINT fixedFileInfoBytes = 0;

			if (VerQueryValueW(versionData.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedFileInfo), &fixedFileInfoBytes) &&
				fixedFileInfo &&
				fixedFileInfoBytes >= sizeof(VS_FIXEDFILEINFO) &&
				fixedFileInfo->dwSignature == 0xFEEF04BD)
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Executable: FileVersionFixed = " + VersionDWORDToString(fixedFileInfo->dwFileVersionMS, fixedFileInfo->dwFileVersionLS));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Executable: ProductVersionFixed = " + VersionDWORDToString(fixedFileInfo->dwProductVersionMS, fixedFileInfo->dwProductVersionLS));
			}

			struct Translation
			{
				WORD language;
				WORD codePage;
			};

			Translation* translations = nullptr;
			UINT translationBytes = 0;
			std::vector<Translation> translationList;

			if (VerQueryValueW(versionData.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translations), &translationBytes) &&
				translations &&
				translationBytes >= sizeof(Translation))
			{
				const UINT translationCount = translationBytes / sizeof(Translation);
				translationList.assign(translations, translations + translationCount);
			}
			else
			{
				translationList.push_back({ 0x0409, 1200 });
			}

			const std::array<const wchar_t*, 7> stringKeys =
			{
				L"CompanyName",
				L"FileDescription",
				L"FileVersion",
				L"InternalName",
				L"OriginalFilename",
				L"ProductName",
				L"ProductVersion",
			};

			for (const wchar_t* key : stringKeys)
			{
				std::string value;

				for (const Translation& translation : translationList)
				{
					value = GetVersionStringValue(versionData, translation.language, translation.codePage, key);

					if (!value.empty())
						break;
				}

				if (!value.empty())
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Executable: " + StringHelper::WideToUtf8(key) + " = " + value);
			}
		}

		void LogSystemInfo()
		{
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: OSVersion=" + GetWindowsVersionString());

			SYSTEM_INFO systemInfo{};
			GetNativeSystemInfo(&systemInfo);

			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: CPU = " + GetCpuBrandString());
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: CPUArchitecture = " + GetProcessorArchitectureName(systemInfo.wProcessorArchitecture));
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: LogicalProcessors = " + std::to_string(systemInfo.dwNumberOfProcessors));
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: ProcessorGroups = " + std::to_string(GetActiveProcessorGroupCount()));
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: ActiveProcessorsAllGroups = " + std::to_string(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: PageSize = " + std::to_string(systemInfo.dwPageSize));
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->System: AllocationGranularity = " + std::to_string(systemInfo.dwAllocationGranularity));

			MEMORYSTATUSEX memoryStatus{};
			memoryStatus.dwLength = sizeof(memoryStatus);

			if (GlobalMemoryStatusEx(&memoryStatus))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->SystemMemory: LoadPercent = " + std::to_string(memoryStatus.dwMemoryLoad));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->SystemMemory: TotalPhysical = " + StringHelper::FormatBytesAsGiB(memoryStatus.ullTotalPhys));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->SystemMemory: AvailablePhysical = " + StringHelper::FormatBytesAsGiB(memoryStatus.ullAvailPhys));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->SystemMemory: TotalPageFile = " + StringHelper::FormatBytesAsGiB(memoryStatus.ullTotalPageFile));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->SystemMemory: AvailablePageFile = " + StringHelper::FormatBytesAsGiB(memoryStatus.ullAvailPageFile));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->SystemMemory: TotalVirtual = " + StringHelper::FormatBytesAsGiB(memoryStatus.ullTotalVirtual));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->SystemMemory: AvailableVirtual = " + StringHelper::FormatBytesAsGiB(memoryStatus.ullAvailVirtual));
			}

			const int primaryWidth = GetSystemMetrics(SM_CXSCREEN);
			const int primaryHeight = GetSystemMetrics(SM_CYSCREEN);
			const int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
			const int virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
			const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
			const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
			const int monitorCount = GetSystemMetrics(SM_CMONITORS);

			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Display: PrimaryResolution = " + std::to_string(primaryWidth) + "x" + std::to_string(primaryHeight));
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Display: VirtualDesktop = " + std::to_string(virtualWidth) + "x" + std::to_string(virtualHeight) + " at " + std::to_string(virtualX) + "," + std::to_string(virtualY));
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->Display: MonitorCount = " + std::to_string(monitorCount));
		}

		void LogAdapterInfo(ID3D12Device* device)
		{
			const LUID deviceLuid = device->GetAdapterLuid();
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Device: AdapterLuid = " + LUIDToString(deviceLuid));

			Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Device: CreateDXGIFactory1 failed; adapter details unavailable");
				return;
			}

			Microsoft::WRL::ComPtr<IDXGIAdapter1> matchingAdapter;
			for (UINT adapterIndex = 0;; ++adapterIndex)
			{
				Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
				if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND)
					break;

				DXGI_ADAPTER_DESC1 adapterDescription{};
				if (FAILED(adapter->GetDesc1(&adapterDescription)))
					continue;

				if (SameLUID(adapterDescription.AdapterLuid, deviceLuid))
				{
					matchingAdapter = adapter;
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: AdapterIndex = " + std::to_string(adapterIndex));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: Description = " + StringHelper::WideToUtf8(adapterDescription.Description));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: VendorId = " + StringHelper::FormatUnsignedHex(adapterDescription.VendorId, 4));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: DeviceId = " + StringHelper::FormatUnsignedHex(adapterDescription.DeviceId, 4));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: SubSysId = " + StringHelper::FormatUnsignedHex(adapterDescription.SubSysId, 8));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: Revision = " + std::to_string(adapterDescription.Revision));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: DedicatedVideoMemory = " + StringHelper::FormatBytesAsGiB(adapterDescription.DedicatedVideoMemory));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: DedicatedSystemMemory = " + StringHelper::FormatBytesAsGiB(adapterDescription.DedicatedSystemMemory));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: SharedSystemMemory = " + StringHelper::FormatBytesAsGiB(adapterDescription.SharedSystemMemory));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: Flags = " + StringHelper::FormatUnsignedHex(adapterDescription.Flags, 8));
					break;
				}
			}

			if (!matchingAdapter)
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPU: matching DXGI adapter not found");
				return;
			}

			Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
			if (SUCCEEDED(matchingAdapter.As(&adapter3)))
			{
				DXGI_QUERY_VIDEO_MEMORY_INFO localMemoryInfo{};
				if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localMemoryInfo)))
				{
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: LocalBudget = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.Budget));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: LocalCurrentUsage = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.CurrentUsage));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: LocalAvailableForReservation = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.AvailableForReservation));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: LocalCurrentReservation = " + StringHelper::FormatBytesAsGiB(localMemoryInfo.CurrentReservation));
				}

				DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalMemoryInfo{};
				if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalMemoryInfo)))
				{
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: NonLocalBudget = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.Budget));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: NonLocalCurrentUsage = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.CurrentUsage));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: NonLocalAvailableForReservation = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.AvailableForReservation));
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->GPUMemory: NonLocalCurrentReservation = " + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.CurrentReservation));
				}
			}
		}

		void LogD3D12FeatureSupport(ID3D12Device* device)
		{
			std::array<D3D_FEATURE_LEVEL, 5> requestedFeatureLevels =
			{
				D3D_FEATURE_LEVEL_12_2,
				D3D_FEATURE_LEVEL_12_1,
				D3D_FEATURE_LEVEL_12_0,
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0,
			};

			D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{};
			featureLevels.NumFeatureLevels = (UINT)requestedFeatureLevels.size();
			featureLevels.pFeatureLevelsRequested = requestedFeatureLevels.data();

			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels))))
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: MaxFeatureLevel = " + D3DFeatureLevelToString(featureLevels.MaxSupportedFeatureLevel));

			D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: DoublePrecisionFloatShaderOps = " + StringHelper::BoolText(options.DoublePrecisionFloatShaderOps != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: OutputMergerLogicOp = " + StringHelper::BoolText(options.OutputMergerLogicOp != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: ResourceBindingTier = " + std::to_string((UINT)options.ResourceBindingTier));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: ResourceHeapTier = " + std::to_string((UINT)options.ResourceHeapTier));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: TiledResourcesTier = " + std::to_string((UINT)options.TiledResourcesTier));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: ConservativeRasterizationTier = " + std::to_string((UINT)options.ConservativeRasterizationTier));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: CrossNodeSharingTier = " + std::to_string((UINT)options.CrossNodeSharingTier));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation = " + StringHelper::BoolText(options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: TypedUAVLoadAdditionalFormats = " + StringHelper::BoolText(options.TypedUAVLoadAdditionalFormats != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: StandardSwizzle64KBSupported = " + StringHelper::BoolText(options.StandardSwizzle64KBSupported != FALSE));
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: WaveOps = " + StringHelper::BoolText(options1.WaveOps != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: WaveLaneCountMin = " + std::to_string(options1.WaveLaneCountMin));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: WaveLaneCountMax = " + std::to_string(options1.WaveLaneCountMax));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: TotalLaneCount = " + std::to_string(options1.TotalLaneCount));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: ExpandedComputeResourceStates = " + StringHelper::BoolText(options1.ExpandedComputeResourceStates != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: Int64ShaderOps = " + StringHelper::BoolText(options1.Int64ShaderOps != FALSE));
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: SRVOnlyTiledResourceTier3 = " + StringHelper::BoolText(options5.SRVOnlyTiledResourceTier3 != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: RenderPassesTier = " + std::to_string((UINT)options5.RenderPassesTier));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: RaytracingTier = " + std::to_string((UINT)options5.RaytracingTier));
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: MeshShaderTier = " + std::to_string((UINT)options7.MeshShaderTier));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: SamplerFeedbackTier = " + std::to_string((UINT)options7.SamplerFeedbackTier));
			}

			D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
			architecture.NodeIndex = 0;
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture))))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: UMA = " + StringHelper::BoolText(architecture.UMA != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: CacheCoherentUMA = " + StringHelper::BoolText(architecture.CacheCoherentUMA != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: IsolatedMMU = " + StringHelper::BoolText(architecture.IsolatedMMU != FALSE));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: TileBasedRenderer = " + StringHelper::BoolText(architecture.TileBasedRenderer != FALSE));
			}

			D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpuVirtualAddressSupport{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gpuVirtualAddressSupport, sizeof(gpuVirtualAddressSupport))))
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: MaxGPUVirtualAddressBitsPerResource = " + std::to_string(gpuVirtualAddressSupport.MaxGPUVirtualAddressBitsPerResource));
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: MaxGPUVirtualAddressBitsPerProcess = " + std::to_string(gpuVirtualAddressSupport.MaxGPUVirtualAddressBitsPerProcess));
			}

			D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
			shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_7;

			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
			{
				shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_6;

				if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
				{
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: HighestShaderModel = unavailable");
				}
				else
				{
					ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: HighestShaderModel = " + D3DShaderModelToString(shaderModel.HighestShaderModel));
				}
			}
			else
			{
				ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: HighestShaderModel = " + D3DShaderModelToString(shaderModel.HighestShaderModel));
			}

			D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature{};
			rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignature, sizeof(rootSignature))))
				rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12Features: HighestRootSignatureVersion = " + std::to_string((UINT)rootSignature.HighestVersion));
		}
	}

	void LogProcessAndSystemInfo()
	{
		std::call_once(processAndSystemInfoLogFlag, []()
		{
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->ProcessAndSystemInfo: begin");
			LogExecutableMetadata();
			LogSystemInfo();
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->ProcessAndSystemInfo: end");
		});
	}

	void LogD3D12DeviceInfo(ID3D12Device* device)
	{
		if (!device)
			return;

		std::call_once(d3d12DeviceInfoLogFlag, [device]()
		{
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12DeviceInfo: begin");
			LogAdapterInfo(device);
			LogD3D12FeatureSupport(device);
			ShaderInjectorIO::WriteToLogFile("SystemInfoLogger->D3D12DeviceInfo: end");
		});
	}
}
