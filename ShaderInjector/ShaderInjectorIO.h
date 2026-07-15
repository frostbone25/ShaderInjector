//ShaderInjectorIO.h
#pragma once

#include <string>
#include <vector>

namespace ShaderInjectorIO
{
	enum class RegistryHive
	{
		CurrentUser,
		LocalMachine,
	};

	//string helpers
	static const std::string extensionBIN = ".bin"; //binary shader bytecode dumps
	static const std::string extensionHLSL = ".hlsl"; //shader source code
	static const std::string extensionBLOB = ".blob"; //compiled shader blob
	static const std::string extensionDXIL = ".dxil"; //disasembled shader bytecode
	static const std::string extensionJSON = ".json"; //shader injector text file
	static const std::string extensionLOG  = ".log"; //shader injector log file
	static const std::string extensionEXE  = ".exe"; //windows executable
	static const char* imguiSettingsName = "ShaderInjectorGUI.ini";
	static const char* injectorSettingsName = "ShaderInjector.ini";

	//generic IO
	bool PathExists(const std::string& path);
	bool FileExists(const std::string& path);
	void DeleteFileIfExists(const std::string& path);
	bool CopyFileIfMissing(const std::string& sourcePath, const std::string& destinationPath);
	bool WriteBinaryFile(const std::string& path, const void* data, size_t size);
	bool ReadTextFile(const std::string& path, std::string& outText);
	bool WriteTextFile(const std::string& path, const std::string& text);
	bool WriteTextFileIfMissing(const std::string& path, const std::string& text);
	bool DirectoryExists(const std::string& path);
	void DirectoryCreate(const std::string& path);
	bool DeleteDirectoryRecursively(const std::string& path);
	bool MovePath(const std::string& sourcePath, const std::string& destinationPath, bool overwriteExisting = false);
	bool OpenDirectory(const std::string& path);
	std::string JoinPath(const std::string& directory, const std::string& childPath);
	std::string DirectoryFromPath(const std::string& path);
	std::string FileNameFromPath(const std::string& path);
	bool IsAbsolutePath(const std::string& path);
	bool PathsEqual(const std::string& left, const std::string& right);
	std::string SanitizeFileStem(const std::string& name);
	std::string ReadRegistryString(RegistryHive hive, const std::string& subKey, const std::string& valueName = "");
	void CollectFilesByExtension(const std::string& directory, const std::string& extension, std::vector<std::string>& outFiles, bool recursive = false, bool includeFullPath = true);

	//directories/paths
	std::string GetGameDirectory();
	std::string GetShaderInjectorDirectory();
	std::string GetDumpsDirectory();
	std::string GetUncapturedPSODirectory();
	std::string GetLogsDirectory();
	std::string GetLogFilePath();
	std::string GetPreviousLogFilePath();
	std::string GetToolsDirectory();
	std::string GetToolPathDXC();
	std::string GetToolPathDXCompiler();
	std::string GetShaderTargetsDirectory();
	std::string GetModifiedShadersDirectory();
	std::string GetModifiedShadersIncludesDirectory();
	std::string GetInjectorSettingsPath();

	//logs
	void RotateLogFiles();
	void WriteToLogFile(const std::string& text);
	void WriteToLogFileError(const std::string& text);
	void WriteToLogFileSuccess(const std::string& text);
	void WriteToLogFileWarning(const std::string& text);

	//shader
	bool GenerateShaderTextDXIL(const std::string shaderBytecodeFilePath);
	bool DumpShaderBytecode(const void* bytecode, size_t size, uint64_t hash, const std::string namePrefix, const std::string& directory);
	bool CompileSourceToDXILBlob(const std::string& shaderSourceFilePath, const std::string& shaderProfile, const std::string& entryPoint, std::string& outBlobPath);
	bool LoadDXILBlobFromDisk(const std::string& shaderBlobFilePath, std::vector<uint8_t>& outBlob);

	//shader internal resources
	static const std::string internalMarkerPixelShaderName = "InternalMarkerPixelShader";
	static const std::string internalNullPixelShaderName = "PixelShaderNull";
	static const std::string internalMarkerComputeShaderName = "InternalMarkerComputeShader";
	std::string GetInternalMarkerPixelShaderSourceCodeFilePath();
	std::string GetInternalMarkerPixelShaderBlobFilePath();
	std::string GetInternalNullPixelShaderSourceCodeFilePath();
	std::string GetInternalNullPixelShaderBlobFilePath();
	std::string GetInternalMarkerComputeShaderSourceCodeFilePath();
	std::string GetInternalMarkerComputeShaderBlobFilePath();
	bool WriteInternalShaderSourceCodeToDisk(std::string shaderSourceFileName, const char* shaderSourceCode);
	bool WriteInternalMarkerPixelShaderSourceCodeToDisk();
	bool WriteInternalNullPixelShaderSourceCodeToDisk();
	bool WriteInternalMarkerComputeShaderSourceCodeToDisk();

	//injector settings
	bool ReadInjectorSettings();
	void CreateInjectorSettings();

	//initalize
	bool Initialize();
}
