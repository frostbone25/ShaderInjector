#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Enum/ShaderSignaturePacking.h"
#include "Enum/RegistryHive.h"
#include "IO/ProcessResult.h"

#include <d3d12.h>

namespace ShaderInjectorIO
{
	//process helpers keep subprocess launch and platform-specific paths in one place.
	ProcessResult RunProcess(const std::string& executablePath, const std::vector<std::string>& arguments, const std::string& standardOutputPath = "");
	std::string GetEnvironmentVariable(const std::string& variableName);
	std::string GetCurrentExecutablePath();
	std::string GetLoadedModulePath(const std::string& loadedModuleName);

	//capture executable, system, and D3D12 details in the log.
	void LogProcessAndSystemInfo();
	void LogD3D12DeviceInfo(ID3D12Device* d3d12Device);

	//shared file suffixes keep generated files consistent throughout IO.
	static const std::string extensionBIN = ".bin";
	static const std::string extensionHLSL = ".hlsl";
	static const std::string extensionBLOB = ".blob";
	static const std::string extensionDXIL = ".dxil";
	static const std::string extensionJSON = ".json";
	static const std::string extensionLOG = ".log";
	static const std::string extensionEXE = ".exe";
	static const char* imguiSettingsName = "ShaderInjectorGUI.ini";
	static const char* injectorSettingsName = "ShaderInjector.ini";

	//convert path strings at the filesystem boundary so callers can use UTF-8 consistently.
	std::filesystem::path PathFromUTF8(const std::string& pathString);
	std::string PathToUTF8(const std::filesystem::path& fileSystemPath);
	bool PathExists(const std::string& pathString);
	bool FileExists(const std::string& filePath);
	void DeleteFileIfExists(const std::string& filePath);
	bool CopyFileIfMissing(const std::string& sourcePath, const std::string& destinationPath);
	bool WriteBinaryFile(const std::string& filePath, const void* fileData, size_t dataByteCount);
	bool ReadTextFile(const std::string& filePath, std::string& fileText);
	bool WriteTextFile(const std::string& filePath, const std::string& fileText);
	bool WriteTextFileIfMissing(const std::string& filePath, const std::string& fileText);
	bool DirectoryExists(const std::string& directoryPath);
	void DirectoryCreate(const std::string& directoryPath);
	bool DeleteDirectoryRecursively(const std::string& directoryPath);
	bool MovePath(const std::string& sourcePath, const std::string& destinationPath, bool overwriteExisting = false);
	bool OpenFile(const std::string& filePath);
	bool OpenDirectory(const std::string& directoryPath);
	std::string JoinPath(const std::string& directory, const std::string& childPath);
	std::string DirectoryFromPath(const std::string& filePath);
	std::string FileNameFromPath(const std::string& filePath);
	std::string MakeRelativePath(const std::string& filePath, const std::string& baseDirectory);
	bool IsAbsolutePath(const std::string& pathString);
	bool PathsEqual(const std::string& left, const std::string& right);
	std::string SanitizeFileStem(const std::string& name);
	std::string ReadRegistryString(RegistryHive hive, const std::string& registrySubKey, const std::string& registryValueName = "");
	void CollectFilesByExtension(const std::string& directory, const std::string& extension, std::vector<std::string>& collectedFilePaths, bool includeSubdirectories = false, bool includeFullPath = true);

	//build application paths from the game directory and shared file suffixes.
	std::string GetGameDirectory();
	std::string GetShaderInjectorDirectory();
	std::string GetInternalDirectory();
	std::string GetDumpsDirectory();
	std::string GetUncapturedPSODirectory();
	std::string GetLogsDirectory();
	std::string GetLogFilePath();
	std::string GetPreviousLogFilePath();
	std::string GetToolsDirectory();
	std::string GetToolPathDXC();
	std::string GetToolPathDXCompiler();
	std::string GetShaderTargetsDirectory();
	std::string GetRenderPassesDirectory();
	std::string GetShaderResourcesDirectory();
	std::string GetModifiedShadersDirectory();
	std::string GetModifiedShadersIncludesDirectory();
	std::string GetShaderConfigurationsPath();
	std::string GetInjectorSettingsPath();

	//log writers serialize entries and apply the configured verbosity rules.
	void RotateLogFiles();
	void WriteToLogFile(const std::string& logText);
	void WriteToLogFileStatus(const std::string& logText);
	void WriteToLogFileError(const std::string& logText);
	void WriteToLogFileSuccess(const std::string& logText);
	void WriteToLogFileWarning(const std::string& logText);

	//compile shader sources and move bytecode between disk and memory.
	bool GenerateShaderTextDXIL(const std::string& shaderBytecodeFilePath);
	bool DumpShaderBytecode(const void* shaderBytecode, size_t bytecodeSize, uint64_t shaderHash, const std::string& shaderNamePrefix, const std::string& dumpDirectory);
	bool CompileSourceToDXILBlob(
		const std::string& shaderSourceFilePath,
		const std::string& shaderProfile,
		const std::string& entryPoint,
		std::string& compiledBlobFilePath,
		ShaderSignaturePacking signaturePacking = ShaderSignaturePacking::PrefixStable);
	bool LoadDXILBlobFromDisk(const std::string& shaderBlobFilePath, std::vector<uint8_t>& shaderBlobBytes);

	//recreate built-in shader source files from the embedded templates.
	static const std::string internalMarkerPixelShaderName = "InternalMarkerPixelShader";
	static const std::string internalNullPixelShaderName = "InternalNullPixelShader";
	static const std::string internalMarkerComputeShaderName = "InternalMarkerComputeShader";
	std::string GetInternalMarkerPixelShaderSourceCodeFilePath();
	std::string GetInternalMarkerPixelShaderBlobFilePath();
	std::string GetInternalNullPixelShaderSourceCodeFilePath();
	std::string GetInternalNullPixelShaderBlobFilePath();
	std::string GetInternalMarkerComputeShaderSourceCodeFilePath();
	std::string GetInternalMarkerComputeShaderBlobFilePath();
	bool WriteInternalShaderSourceCodeToDisk(const std::string& shaderSourceFilePath, const std::string& shaderSourceText);
	bool WriteInternalMarkerPixelShaderSourceCodeToDisk();
	bool WriteInternalNullPixelShaderSourceCodeToDisk();
	bool WriteInternalMarkerComputeShaderSourceCodeToDisk();

	//load and save injector settings in the on-disk INI format.
	bool ReadInjectorSettings();
	void CreateInjectorSettings();
	bool WriteInjectorSettings();
	bool WriteInjectorMenuScale(float menuScale);

	//prepare the directories and settings the rest of the injector relies on.
	bool Initialize();
}
