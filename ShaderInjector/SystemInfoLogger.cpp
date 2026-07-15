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
#include "ProcessRunner.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"

#pragma comment(lib, "Version.lib")

namespace SystemInfoLogger
{
	namespace
	{
		std::once_flag processAndSystemInfoLogFlag;
		std::once_flag d3d12DeviceInfoLogFlag;

		void LogLine(const std::string& text)
		{
			ShaderInjectorIO::WriteToLogFile(text);
		}

		std::string VersionDwordToString(DWORD mostSignificant, DWORD leastSignificant)
		{
			std::ostringstream stream;
			stream
				<< HIWORD(mostSignificant) << "."
				<< LOWORD(mostSignificant) << "."
				<< HIWORD(leastSignificant) << "."
				<< LOWORD(leastSignificant);
			return stream.str();
		}

		std::string LuidToString(LUID luid)
		{
			std::ostringstream stream;
			stream << StringHelper::FormatUnsignedHex((uint32_t)luid.HighPart, 8) << ":"
				<< StringHelper::FormatUnsignedHex(luid.LowPart, 8);
			return stream.str();
		}

		bool SameLuid(LUID left, LUID right)
		{
			return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
		}

		std::string GetProcessorArchitectureName(WORD architecture)
		{
			switch (architecture)
			{
				case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
				case PROCESSOR_ARCHITECTURE_ARM: return "ARM";
				case PROCESSOR_ARCHITECTURE_ARM64: return "ARM64";
				case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
				default: return "Unknown";
			}
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
			RtlGetVersionFunction rtlGetVersion =
				ntdllModule ? reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdllModule, "RtlGetVersion")) : nullptr;

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
				LogLine("SystemInfoLogger->Executable: path unavailable");
				return;
			}

			LogLine("SystemInfoLogger->Executable: Path=" + StringHelper::WideToUtf8(executablePath));

			DWORD ignoredHandle = 0;
			const DWORD versionDataBytes = GetFileVersionInfoSizeW(executablePath.c_str(), &ignoredHandle);
			if (versionDataBytes == 0)
			{
				LogLine("SystemInfoLogger->Executable: no version metadata found");
				return;
			}

			std::vector<BYTE> versionData(versionDataBytes);
			if (!GetFileVersionInfoW(executablePath.c_str(), 0, versionDataBytes, versionData.data()))
			{
				LogLine("SystemInfoLogger->Executable: failed to read version metadata");
				return;
			}

			VS_FIXEDFILEINFO* fixedFileInfo = nullptr;
			UINT fixedFileInfoBytes = 0;
			if (VerQueryValueW(versionData.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedFileInfo), &fixedFileInfoBytes) &&
				fixedFileInfo &&
				fixedFileInfoBytes >= sizeof(VS_FIXEDFILEINFO) &&
				fixedFileInfo->dwSignature == 0xFEEF04BD)
			{
				LogLine("SystemInfoLogger->Executable: FileVersionFixed=" + VersionDwordToString(fixedFileInfo->dwFileVersionMS, fixedFileInfo->dwFileVersionLS));
				LogLine("SystemInfoLogger->Executable: ProductVersionFixed=" + VersionDwordToString(fixedFileInfo->dwProductVersionMS, fixedFileInfo->dwProductVersionLS));
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
					LogLine("SystemInfoLogger->Executable: " + StringHelper::WideToUtf8(key) + "=" + value);
			}
		}

		std::string FeatureLevelToString(D3D_FEATURE_LEVEL featureLevel)
		{
			switch (featureLevel)
			{
				case D3D_FEATURE_LEVEL_12_2: return "12_2";
				case D3D_FEATURE_LEVEL_12_1: return "12_1";
				case D3D_FEATURE_LEVEL_12_0: return "12_0";
				case D3D_FEATURE_LEVEL_11_1: return "11_1";
				case D3D_FEATURE_LEVEL_11_0: return "11_0";
				default: return std::to_string((UINT)featureLevel);
			}
		}

		std::string ShaderModelToString(D3D_SHADER_MODEL shaderModel)
		{
			switch (shaderModel)
			{
				case D3D_SHADER_MODEL_6_7: return "6_7";
				case D3D_SHADER_MODEL_6_6: return "6_6";
				case D3D_SHADER_MODEL_6_5: return "6_5";
				case D3D_SHADER_MODEL_6_4: return "6_4";
				case D3D_SHADER_MODEL_6_3: return "6_3";
				case D3D_SHADER_MODEL_6_2: return "6_2";
				case D3D_SHADER_MODEL_6_1: return "6_1";
				case D3D_SHADER_MODEL_6_0: return "6_0";
				default: return std::to_string((UINT)shaderModel);
			}
		}

		void LogSystemInfo()
		{
			LogLine("SystemInfoLogger->System: OSVersion=" + GetWindowsVersionString());

			SYSTEM_INFO systemInfo{};
			GetNativeSystemInfo(&systemInfo);

			LogLine("SystemInfoLogger->System: CPU=" + GetCpuBrandString());
			LogLine("SystemInfoLogger->System: CPUArchitecture=" + GetProcessorArchitectureName(systemInfo.wProcessorArchitecture));
			LogLine("SystemInfoLogger->System: LogicalProcessors=" + std::to_string(systemInfo.dwNumberOfProcessors));
			LogLine("SystemInfoLogger->System: ProcessorGroups=" + std::to_string(GetActiveProcessorGroupCount()));
			LogLine("SystemInfoLogger->System: ActiveProcessorsAllGroups=" + std::to_string(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
			LogLine("SystemInfoLogger->System: PageSize=" + std::to_string(systemInfo.dwPageSize));
			LogLine("SystemInfoLogger->System: AllocationGranularity=" + std::to_string(systemInfo.dwAllocationGranularity));

			MEMORYSTATUSEX memoryStatus{};
			memoryStatus.dwLength = sizeof(memoryStatus);
			if (GlobalMemoryStatusEx(&memoryStatus))
			{
				LogLine("SystemInfoLogger->SystemMemory: LoadPercent=" + std::to_string(memoryStatus.dwMemoryLoad));
				LogLine("SystemInfoLogger->SystemMemory: TotalPhysical=" + StringHelper::FormatBytesAsGiB(memoryStatus.ullTotalPhys));
				LogLine("SystemInfoLogger->SystemMemory: AvailablePhysical=" + StringHelper::FormatBytesAsGiB(memoryStatus.ullAvailPhys));
				LogLine("SystemInfoLogger->SystemMemory: TotalPageFile=" + StringHelper::FormatBytesAsGiB(memoryStatus.ullTotalPageFile));
				LogLine("SystemInfoLogger->SystemMemory: AvailablePageFile=" + StringHelper::FormatBytesAsGiB(memoryStatus.ullAvailPageFile));
				LogLine("SystemInfoLogger->SystemMemory: TotalVirtual=" + StringHelper::FormatBytesAsGiB(memoryStatus.ullTotalVirtual));
				LogLine("SystemInfoLogger->SystemMemory: AvailableVirtual=" + StringHelper::FormatBytesAsGiB(memoryStatus.ullAvailVirtual));
			}

			const int primaryWidth = GetSystemMetrics(SM_CXSCREEN);
			const int primaryHeight = GetSystemMetrics(SM_CYSCREEN);
			const int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
			const int virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
			const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
			const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
			const int monitorCount = GetSystemMetrics(SM_CMONITORS);

			LogLine("SystemInfoLogger->Display: PrimaryResolution=" + std::to_string(primaryWidth) + "x" + std::to_string(primaryHeight));
			LogLine(
				"SystemInfoLogger->Display: VirtualDesktop=" +
				std::to_string(virtualWidth) + "x" + std::to_string(virtualHeight) +
				" at " + std::to_string(virtualX) + "," + std::to_string(virtualY));
			LogLine("SystemInfoLogger->Display: MonitorCount=" + std::to_string(monitorCount));
		}

		void LogAdapterInfo(ID3D12Device* device)
		{
			const LUID deviceLuid = device->GetAdapterLuid();
			LogLine("SystemInfoLogger->D3D12Device: AdapterLuid=" + LuidToString(deviceLuid));

			Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			{
				LogLine("SystemInfoLogger->D3D12Device: CreateDXGIFactory1 failed; adapter details unavailable");
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

				if (SameLuid(adapterDescription.AdapterLuid, deviceLuid))
				{
					matchingAdapter = adapter;
					LogLine("SystemInfoLogger->GPU: AdapterIndex=" + std::to_string(adapterIndex));
					LogLine("SystemInfoLogger->GPU: Description=" + StringHelper::WideToUtf8(adapterDescription.Description));
					LogLine("SystemInfoLogger->GPU: VendorId=" + StringHelper::FormatUnsignedHex(adapterDescription.VendorId, 4));
					LogLine("SystemInfoLogger->GPU: DeviceId=" + StringHelper::FormatUnsignedHex(adapterDescription.DeviceId, 4));
					LogLine("SystemInfoLogger->GPU: SubSysId=" + StringHelper::FormatUnsignedHex(adapterDescription.SubSysId, 8));
					LogLine("SystemInfoLogger->GPU: Revision=" + std::to_string(adapterDescription.Revision));
					LogLine("SystemInfoLogger->GPU: DedicatedVideoMemory=" + StringHelper::FormatBytesAsGiB(adapterDescription.DedicatedVideoMemory));
					LogLine("SystemInfoLogger->GPU: DedicatedSystemMemory=" + StringHelper::FormatBytesAsGiB(adapterDescription.DedicatedSystemMemory));
					LogLine("SystemInfoLogger->GPU: SharedSystemMemory=" + StringHelper::FormatBytesAsGiB(adapterDescription.SharedSystemMemory));
					LogLine("SystemInfoLogger->GPU: Flags=" + StringHelper::FormatUnsignedHex(adapterDescription.Flags, 8));
					break;
				}
			}

			if (!matchingAdapter)
			{
				LogLine("SystemInfoLogger->GPU: matching DXGI adapter not found");
				return;
			}

			Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
			if (SUCCEEDED(matchingAdapter.As(&adapter3)))
			{
				DXGI_QUERY_VIDEO_MEMORY_INFO localMemoryInfo{};
				if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localMemoryInfo)))
				{
					LogLine("SystemInfoLogger->GPUMemory: LocalBudget=" + StringHelper::FormatBytesAsGiB(localMemoryInfo.Budget));
					LogLine("SystemInfoLogger->GPUMemory: LocalCurrentUsage=" + StringHelper::FormatBytesAsGiB(localMemoryInfo.CurrentUsage));
					LogLine("SystemInfoLogger->GPUMemory: LocalAvailableForReservation=" + StringHelper::FormatBytesAsGiB(localMemoryInfo.AvailableForReservation));
					LogLine("SystemInfoLogger->GPUMemory: LocalCurrentReservation=" + StringHelper::FormatBytesAsGiB(localMemoryInfo.CurrentReservation));
				}

				DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalMemoryInfo{};
				if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalMemoryInfo)))
				{
					LogLine("SystemInfoLogger->GPUMemory: NonLocalBudget=" + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.Budget));
					LogLine("SystemInfoLogger->GPUMemory: NonLocalCurrentUsage=" + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.CurrentUsage));
					LogLine("SystemInfoLogger->GPUMemory: NonLocalAvailableForReservation=" + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.AvailableForReservation));
					LogLine("SystemInfoLogger->GPUMemory: NonLocalCurrentReservation=" + StringHelper::FormatBytesAsGiB(nonLocalMemoryInfo.CurrentReservation));
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
				LogLine("SystemInfoLogger->D3D12Features: MaxFeatureLevel=" + FeatureLevelToString(featureLevels.MaxSupportedFeatureLevel));

			D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
			{
				LogLine("SystemInfoLogger->D3D12Features: DoublePrecisionFloatShaderOps=" + StringHelper::BoolText(options.DoublePrecisionFloatShaderOps != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: OutputMergerLogicOp=" + StringHelper::BoolText(options.OutputMergerLogicOp != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: ResourceBindingTier=" + std::to_string((UINT)options.ResourceBindingTier));
				LogLine("SystemInfoLogger->D3D12Features: ResourceHeapTier=" + std::to_string((UINT)options.ResourceHeapTier));
				LogLine("SystemInfoLogger->D3D12Features: TiledResourcesTier=" + std::to_string((UINT)options.TiledResourcesTier));
				LogLine("SystemInfoLogger->D3D12Features: ConservativeRasterizationTier=" + std::to_string((UINT)options.ConservativeRasterizationTier));
				LogLine("SystemInfoLogger->D3D12Features: CrossNodeSharingTier=" + std::to_string((UINT)options.CrossNodeSharingTier));
				LogLine("SystemInfoLogger->D3D12Features: VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation=" + StringHelper::BoolText(options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: TypedUAVLoadAdditionalFormats=" + StringHelper::BoolText(options.TypedUAVLoadAdditionalFormats != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: StandardSwizzle64KBSupported=" + StringHelper::BoolText(options.StandardSwizzle64KBSupported != FALSE));
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))))
			{
				LogLine("SystemInfoLogger->D3D12Features: WaveOps=" + StringHelper::BoolText(options1.WaveOps != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: WaveLaneCountMin=" + std::to_string(options1.WaveLaneCountMin));
				LogLine("SystemInfoLogger->D3D12Features: WaveLaneCountMax=" + std::to_string(options1.WaveLaneCountMax));
				LogLine("SystemInfoLogger->D3D12Features: TotalLaneCount=" + std::to_string(options1.TotalLaneCount));
				LogLine("SystemInfoLogger->D3D12Features: ExpandedComputeResourceStates=" + StringHelper::BoolText(options1.ExpandedComputeResourceStates != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: Int64ShaderOps=" + StringHelper::BoolText(options1.Int64ShaderOps != FALSE));
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
			{
				LogLine("SystemInfoLogger->D3D12Features: SRVOnlyTiledResourceTier3=" + StringHelper::BoolText(options5.SRVOnlyTiledResourceTier3 != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: RenderPassesTier=" + std::to_string((UINT)options5.RenderPassesTier));
				LogLine("SystemInfoLogger->D3D12Features: RaytracingTier=" + std::to_string((UINT)options5.RaytracingTier));
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
			{
				LogLine("SystemInfoLogger->D3D12Features: MeshShaderTier=" + std::to_string((UINT)options7.MeshShaderTier));
				LogLine("SystemInfoLogger->D3D12Features: SamplerFeedbackTier=" + std::to_string((UINT)options7.SamplerFeedbackTier));
			}

			D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
			architecture.NodeIndex = 0;
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture))))
			{
				LogLine("SystemInfoLogger->D3D12Features: UMA=" + StringHelper::BoolText(architecture.UMA != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: CacheCoherentUMA=" + StringHelper::BoolText(architecture.CacheCoherentUMA != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: IsolatedMMU=" + StringHelper::BoolText(architecture.IsolatedMMU != FALSE));
				LogLine("SystemInfoLogger->D3D12Features: TileBasedRenderer=" + StringHelper::BoolText(architecture.TileBasedRenderer != FALSE));
			}

			D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpuVirtualAddressSupport{};
			if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gpuVirtualAddressSupport, sizeof(gpuVirtualAddressSupport))))
			{
				LogLine("SystemInfoLogger->D3D12Features: MaxGPUVirtualAddressBitsPerResource=" + std::to_string(gpuVirtualAddressSupport.MaxGPUVirtualAddressBitsPerResource));
				LogLine("SystemInfoLogger->D3D12Features: MaxGPUVirtualAddressBitsPerProcess=" + std::to_string(gpuVirtualAddressSupport.MaxGPUVirtualAddressBitsPerProcess));
			}

			D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
			shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_7;
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
			{
				shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_6;
				if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
				{
					LogLine("SystemInfoLogger->D3D12Features: HighestShaderModel=unavailable");
				}
				else
				{
					LogLine("SystemInfoLogger->D3D12Features: HighestShaderModel=" + ShaderModelToString(shaderModel.HighestShaderModel));
				}
			}
			else
			{
				LogLine("SystemInfoLogger->D3D12Features: HighestShaderModel=" + ShaderModelToString(shaderModel.HighestShaderModel));
			}

			D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature{};
			rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignature, sizeof(rootSignature))))
				rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
			LogLine("SystemInfoLogger->D3D12Features: HighestRootSignatureVersion=" + std::to_string((UINT)rootSignature.HighestVersion));
		}
	}

	void LogProcessAndSystemInfo()
	{
		std::call_once(processAndSystemInfoLogFlag, []()
		{
			LogLine("SystemInfoLogger->ProcessAndSystemInfo: begin");
			LogExecutableMetadata();
			LogSystemInfo();
			LogLine("SystemInfoLogger->ProcessAndSystemInfo: end");
		});
	}

	void LogD3D12DeviceInfo(ID3D12Device* device)
	{
		if (!device)
			return;

		std::call_once(d3d12DeviceInfoLogFlag, [device]()
		{
			LogLine("SystemInfoLogger->D3D12DeviceInfo: begin");
			LogAdapterInfo(device);
			LogD3D12FeatureSupport(device);
			LogLine("SystemInfoLogger->D3D12DeviceInfo: end");
		});
	}
}
