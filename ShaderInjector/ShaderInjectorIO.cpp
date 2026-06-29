//ShaderInjectorIO.cpp
#include "ShaderInjectorIO.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#if defined(_WIN32)
	#include <Windows.h>
#else
	#include <limits.h>
	#include <unistd.h>
#endif

//3RD Party
#include "inicpp.h"

//custom
#include "ShaderTemplates.h"
#include "Globals.h"
#include "ProcessRunner.h"

namespace ShaderInjectorIO
{
	namespace
	{
		namespace FileSystem = std::filesystem;

		FileSystem::path PathFromUtf8(const std::string& path)
		{
			std::string normalizedPath = path;
#if !defined(_WIN32)
			// Accept replacement metadata produced on Windows when inspecting or
			// migrating it on a Unix-like host.
			std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
#endif
			return FileSystem::u8path(normalizedPath);
		}

		std::string PathToUtf8(const FileSystem::path& path)
		{
			return path.u8string();
		}

		std::string Lowercase(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return text;
		}

		std::mutex gLogMutex;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	bool PathExists(const std::string& path)
	{
		std::error_code error;
		return !path.empty() && FileSystem::exists(PathFromUtf8(path), error);
	}

	bool FileExists(const std::string& filePath)
	{
		std::error_code error;
		return !filePath.empty() && FileSystem::is_regular_file(PathFromUtf8(filePath), error);
	}

	void DeleteFileIfExists(const std::string& filePath)
	{
		std::error_code error;
		if (!filePath.empty())
			FileSystem::remove(PathFromUtf8(filePath), error);
	}

	bool CopyFileIfMissing(const std::string& sourcePath, const std::string& destinationPath)
	{
		if (!FileExists(sourcePath))
			return false;

		if (FileExists(destinationPath))
			return true;

		std::error_code error;
		return FileSystem::copy_file(
			PathFromUtf8(sourcePath),
			PathFromUtf8(destinationPath),
			FileSystem::copy_options::none,
			error);
	}

	bool WriteBinaryFile(const std::string& filePath, const void* data, size_t size)
	{
		if (!data || size == 0)
			return false;

		std::ofstream file(PathFromUtf8(filePath), std::ios::binary | std::ios::trunc);
		if (!file.is_open())
			return false;

		file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
		return !file.fail();
	}

	bool WriteTextFileIfMissing(const std::string& filePath, const std::string& text)
	{
		if (FileExists(filePath))
			return true;

		std::ofstream file(PathFromUtf8(filePath), std::ios::out | std::ios::trunc);

		if (!file.is_open())
			return false;

		file << text;
		return !file.fail();
	}

	bool DirectoryExists(const std::string& directoryPath)
	{
		std::error_code error;
		return !directoryPath.empty() && FileSystem::is_directory(PathFromUtf8(directoryPath), error);
	}

	void DirectoryCreate(const std::string& directoryPath)
	{
		if (directoryPath.empty())
			return;

		std::error_code error;
		FileSystem::create_directories(PathFromUtf8(directoryPath), error);
	}

	std::string JoinPath(const std::string& directory, const std::string& childPath)
	{
		if (directory.empty())
			return childPath;

		if (childPath.empty())
			return directory;

		return PathToUtf8(PathFromUtf8(directory) / PathFromUtf8(childPath));
	}

	std::string DirectoryFromPath(const std::string& path)
	{
		return PathToUtf8(PathFromUtf8(path).parent_path());
	}

	std::string FileNameFromPath(const std::string& path)
	{
		return PathToUtf8(PathFromUtf8(path).filename());
	}

	bool IsAbsolutePath(const std::string& path)
	{
		if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
			return true;

		if (path.size() >= 2 && ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/')))
			return true;

		return !path.empty() && PathFromUtf8(path).is_absolute();
	}

	void CollectFilesByExtension(const std::string& directory, const std::string& extension, std::vector<std::string>& outFiles, bool recursive, bool includeFullPath)
	{
		if (directory.empty() || extension.empty() || !DirectoryExists(directory))
			return;

		const std::string expectedExtension = Lowercase(extension.front() == '.' ? extension : "." + extension);
		const FileSystem::directory_options options = FileSystem::directory_options::skip_permission_denied;
		std::error_code error;

		auto collectEntry = [&](const FileSystem::directory_entry& entry)
		{
			if (!entry.is_regular_file(error) || Lowercase(PathToUtf8(entry.path().extension())) != expectedExtension)
				return;

			outFiles.push_back(PathToUtf8(includeFullPath ? entry.path() : entry.path().filename()));
		};

		if (recursive)
		{
			for (FileSystem::recursive_directory_iterator iterator(PathFromUtf8(directory), options, error), end; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					error.clear();
					continue;
				}
				collectEntry(*iterator);
			}
		}
		else
		{
			for (FileSystem::directory_iterator iterator(PathFromUtf8(directory), options, error), end; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					error.clear();
					continue;
				}
				collectEntry(*iterator);
			}
		}
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DIRECTORIES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DIRECTORIES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DIRECTORIES |||||||||||||||||||||||||||||||||||||||||||||||||||||

	std::string GetGameDirectory()
	{
#if defined(_WIN32)
		std::vector<wchar_t> pathBuffer(1024);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
			if (length == 0)
				return {};

			if (length < pathBuffer.size() - 1)
				return PathToUtf8(FileSystem::path(std::wstring(pathBuffer.data(), length)).parent_path());

			pathBuffer.resize(pathBuffer.size() * 2);
		}
#else
		std::vector<char> pathBuffer(PATH_MAX + 1, '\0');
		const ssize_t length = readlink("/proc/self/exe", pathBuffer.data(), PATH_MAX);
		if (length <= 0)
			return {};

		return PathToUtf8(FileSystem::path(std::string(pathBuffer.data(), static_cast<size_t>(length))).parent_path());
#endif
	}

	std::string GetShaderInjectorDirectory()
	{
		return JoinPath(GetGameDirectory(), "ShaderInjector");
	}

	std::string GetLogsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "Logs");
	}

	std::string GetLogFilePath()
	{
		return JoinPath(GetLogsDirectory(), "ShaderInjector" + extensionLOG);
	}

	std::string GetPreviousLogFilePath()
	{
		return JoinPath(GetLogsDirectory(), "ShaderLogPrevious" + extensionLOG);
	}

	std::string GetDumpsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "Dumps");
	}

	std::string GetUncapturedPSODirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "UncapturedPSOs");
	}

	std::string GetToolsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "Tools");
	}

	std::string GetToolPathDXC()
	{
#if defined(_WIN32)
		return JoinPath(GetToolsDirectory(), "dxc" + extensionEXE);
#else
		return JoinPath(GetToolsDirectory(), "dxc");
#endif
	}

	std::string GetShaderReplacementsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "ShaderReplacements");
	}

	std::string GetShaderSourcesDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "ShaderSources");
	}

	std::string GetShaderSourcesDirectory(const std::string& shaderTypeDirectoryName)
	{
		return JoinPath(GetShaderSourcesDirectory(), shaderTypeDirectoryName);
	}

	std::string GetInjectorSettingsPath()
	{
		return JoinPath(GetGameDirectory(), injectorSettingsName);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	void RotateLogFiles()
	{
		std::lock_guard<std::mutex> lock(gLogMutex);

		// Rotation runs before the rest of IO initialization, so ensure the log
		// directory exists even on the first launch.
		std::error_code error;
		FileSystem::create_directories(PathFromUtf8(GetLogsDirectory()), error);

		const FileSystem::path currentLogPath = PathFromUtf8(GetLogFilePath());
		const FileSystem::path previousLogPath = PathFromUtf8(GetPreviousLogFilePath());

		error.clear();
		FileSystem::remove(previousLogPath, error);

		error.clear();
		if (FileSystem::is_regular_file(currentLogPath, error))
		{
			error.clear();
			FileSystem::rename(currentLogPath, previousLogPath, error);

			// A same-directory rename should normally succeed. Retain a copy fallback
			// for filesystems exposed through Wine/Proton that implement rename poorly.
			if (error)
			{
				error.clear();
				FileSystem::copy_file(currentLogPath, previousLogPath, FileSystem::copy_options::overwrite_existing, error);
				if (!error)
					FileSystem::remove(currentLogPath, error);
			}
		}

		std::ofstream currentLog(currentLogPath, std::ios::trunc);
	}


	void WriteToLogFile(const std::string& text)
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		std::ofstream logFile(PathFromUtf8(GetLogFilePath()), std::ios::app);
		if (!logFile.is_open())
			return;

		const std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm localTime{};
#if defined(_WIN32)
		localtime_s(&localTime, &currentTime);
#else
		localtime_r(&currentTime, &localTime);
#endif

		char timestamp[16]{};
		std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &localTime);
		logFile << '[' << timestamp << "] " << text << '\n';
	}

	//quick wrapper, just to knock down our log strings in the code because my eyes hurt
	void WriteToLogFileError(const std::string& text)
	{
		WriteToLogFile("[ERROR] " + text);
	}

	//quick wrapper, just to knock down our log strings in the code because my eyes hurt
	void WriteToLogFileSuccess(const std::string& text)
	{
		WriteToLogFile("[SUCCESS] " + text);
	}

	//quick wrapper, just to knock down our log strings in the code because my eyes hurt
	void WriteToLogFileWarning(const std::string& text)
	{
		WriteToLogFile("[WARNING] " + text);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//given the file path of a compiled shader bytecode blob, use dxc to disassemble into a somewhat readable dxil text file
	bool GenerateShaderTextDXIL(const std::string shaderBytecodeFilePath)
	{
		if (!FileExists(shaderBytecodeFilePath))
		{
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: error! shader bytecode file not found! " + shaderBytecodeFilePath);
			return false;
		}

		const std::string dxcPath = GetToolPathDXC();

		if (!FileExists(dxcPath))
		{
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: DXC executable was not found: " + dxcPath);
			return false;
		}

		FileSystem::path outputPath = PathFromUtf8(shaderBytecodeFilePath);
		outputPath.replace_extension(extensionDXIL);
		const std::string dxilTextPath = PathToUtf8(outputPath);

		const ProcessRunner::ProcessResult processResult = ProcessRunner::Run(
			dxcPath,
			{ "-dumpbin", shaderBytecodeFilePath },
			dxilTextPath);

		if (!processResult.Succeeded())
		{
			DeleteFileIfExists(dxilTextPath);
			WriteToLogFileError(
				"ShaderInjectorIO->GenerateShaderTextDXIL: DXC failed (exit=" +
				std::to_string(processResult.exitCode) + "): " + processResult.errorMessage);
			return false;
		}

		return FileExists(dxilTextPath);
	}

	//given raw bytecode from memory, serialize/dump it to the disk in a given directory
	bool DumpShaderBytecode(const void* bytecode, size_t size, uint64_t hash, const std::string namePrefix, const std::string& directory)
	{
		if (!bytecode || size == 0)
		{
			WriteToLogFileError("ShaderInjectorIO->DumpShaderBytecode: error! given shader bytecode is null or size is 0!");
			return false;
		}

		char filename[256];
		sprintf_s(filename, "%016llX.bin", (unsigned long long)hash);
		const std::string path = JoinPath(directory, namePrefix + "_" + filename);

		if (!WriteBinaryFile(path, bytecode, size))
			return false;

		return GenerateShaderTextDXIL(path);
	}

	//given raw HLSL human readable shader source code, compile it into a shader blob
	bool CompileSourceToDXILBlob(const std::string& shaderSourceFilePath, const std::string& shaderProfile, const std::string& entryPoint, std::string& outBlobPath)
	{
		if (!FileExists(shaderSourceFilePath))
		{
			WriteToLogFileError("ShaderInjectorIO->CompileSourceToDXILBlob: error! shader source file not found! " + shaderSourceFilePath);
			return false;
		}

		const std::string dxcPath = GetToolPathDXC();

		if (!FileExists(dxcPath))
		{
			WriteToLogFileError("ShaderInjectorIO->CompileSourceToDXILBlob: DXC executable was not found: " + dxcPath);
			return false;
		}

		if (outBlobPath.empty())
		{
			FileSystem::path blobPath = PathFromUtf8(shaderSourceFilePath);
			blobPath.replace_extension(extensionBLOB);
			outBlobPath = PathToUtf8(blobPath);
		}

		const std::string temporaryBlobPath = outBlobPath + ".compiling";
		DeleteFileIfExists(temporaryBlobPath);

		const ProcessRunner::ProcessResult processResult = ProcessRunner::Run(
			dxcPath,
			{ "-T", shaderProfile, "-E", entryPoint, shaderSourceFilePath, "-Fo", temporaryBlobPath });

		if (!processResult.Succeeded())
		{
			DeleteFileIfExists(temporaryBlobPath);
			WriteToLogFileError(
				"ShaderInjectorIO->CompileSourceToDXILBlob: DXC failed (exit=" +
				std::to_string(processResult.exitCode) + "): " + processResult.errorMessage);
			return false;
		}

		if (!FileExists(temporaryBlobPath))
		{
			WriteToLogFileError("ShaderInjectorIO->CompileSourceToDXILBlob: DXC reported success but did not create " + temporaryBlobPath);
			return false;
		}

		std::error_code replaceError;
		FileSystem::copy_file(
			PathFromUtf8(temporaryBlobPath),
			PathFromUtf8(outBlobPath),
			FileSystem::copy_options::overwrite_existing,
			replaceError);
		DeleteFileIfExists(temporaryBlobPath);

		if (replaceError)
		{
			WriteToLogFileError("ShaderInjectorIO->CompileSourceToDXILBlob: could not replace compiled blob: " + replaceError.message());
			return false;
		}

		return FileExists(outBlobPath);
	}

	//given the path of a compiled shader blob, load it into memory
	bool LoadDXILBlobFromDisk(const std::string& shaderBlobFilePath, std::vector<uint8_t>& outBlob)
	{
		if (!FileExists(shaderBlobFilePath))
		{
			WriteToLogFileError("ShaderInjectorIO->LoadDXILBlobFromDisk: error! shader blob file not found! " + shaderBlobFilePath);
			return false;
		}

		// Clear stale bytes before loading the compiled DXIL blob into memory.
		outBlob.clear();

		std::ifstream shaderBlobFile(PathFromUtf8(shaderBlobFilePath), std::ios::binary | std::ios::ate);
		if (!shaderBlobFile.is_open())
			return false;

		const std::streamsize fileSize = shaderBlobFile.tellg();
		if (fileSize <= 0)
			return false;

		outBlob.resize(static_cast<size_t>(fileSize));
		shaderBlobFile.seekg(0, std::ios::beg);
		return static_cast<bool>(shaderBlobFile.read(reinterpret_cast<char*>(outBlob.data()), fileSize));
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||

	std::string GetInternalMarkerPixelShaderSourceCodeFilePath() 
	{
		return JoinPath(GetShaderInjectorDirectory(), internalMarkerPixelShaderName + extensionHLSL);
	}

	std::string GetInternalMarkerPixelShaderBlobFilePath()
	{
		return JoinPath(GetShaderInjectorDirectory(), internalMarkerPixelShaderName + extensionBLOB);
	}

	std::string GetInternalNullPixelShaderSourceCodeFilePath()
	{
		return JoinPath(GetShaderInjectorDirectory(), internalNullPixelShaderName + extensionHLSL);
	}

	std::string GetInternalNullPixelShaderBlobFilePath()
	{
		return JoinPath(GetShaderInjectorDirectory(), internalNullPixelShaderName + extensionBLOB);
	}

	std::string GetInternalMarkerComputeShaderSourceCodeFilePath()
	{
		return JoinPath(GetShaderInjectorDirectory(), internalMarkerComputeShaderName + extensionHLSL);
	}

	std::string GetInternalMarkerComputeShaderBlobFilePath()
	{
		return JoinPath(GetShaderInjectorDirectory(), internalMarkerComputeShaderName + extensionBLOB);
	}

	bool WriteInternalShaderSourceCodeToDisk(std::string shaderSourceFilePath, const char* shaderSourceCode)
	{
		//NOTE: we open with trunc so we overwrite what was originally there in the file
		//for "internal" shaders used by the shader injector, want to make sure that we are starting clean and fresh
		//as it's very possible that it got tampered with
		std::ofstream file(PathFromUtf8(shaderSourceFilePath), std::ios::out | std::ios::trunc);

		if (!file.is_open())
		{
			WriteToLogFileError("ShaderInjectorIO->WriteInternalShaderCodeToDisk: failed to open file: " + shaderSourceFilePath);
			return false;
		}

		file << shaderSourceCode;

		if (file.fail())
		{
			WriteToLogFileError("ShaderInjectorIO->WriteInternalShaderCodeToDisk: failed to write file: " + shaderSourceFilePath);
			return false;
		}

		file.close();

		return true;
	}

	bool WriteInternalMarkerPixelShaderSourceCodeToDisk()
	{
		return WriteInternalShaderSourceCodeToDisk(GetInternalMarkerPixelShaderSourceCodeFilePath(), ShaderTemplates::internalMarkerPixelShaderSourceCode);
	}

	bool WriteInternalNullPixelShaderSourceCodeToDisk()
	{
		return WriteInternalShaderSourceCodeToDisk(GetInternalNullPixelShaderSourceCodeFilePath(), ShaderTemplates::internalNullPixelShaderSourceCode);
	}

	bool WriteInternalMarkerComputeShaderSourceCodeToDisk()
	{
		return WriteInternalShaderSourceCodeToDisk(GetInternalMarkerComputeShaderSourceCodeFilePath(), ShaderTemplates::internalMarkerComputeShaderSourceCode);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INJECTOR SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INJECTOR SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INJECTOR SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//returns true for success, false for failure
	bool ReadInjectorSettings() 
	{
		std::string injectorSettingsPath = GetInjectorSettingsPath();

		if (!FileExists(injectorSettingsPath))
		{
			WriteToLogFileWarning("ShaderInjectorIO->ReadInjectorSettings: injector settings ini not found! using default settings... ");
			return false;
		}

		try
		{
			ini::IniFile injectorSettingsINI;
			injectorSettingsINI.load(injectorSettingsPath);

			int keyOpenShaderInjectorGUI = injectorSettingsINI["InjectorSettings"]["OpenMenuKey"].as<int>();
			int keyToggleShaderInjector = injectorSettingsINI["InjectorSettings"]["ToggleInjectorKey"].as<int>();
			bool gShaderInjectorEnabled = injectorSettingsINI["InjectorSettings"]["InjectorEnabled"].as<bool>();
			bool gShowShaderInjectorGUI = injectorSettingsINI["InjectorSettings"]["MenuOpen"].as<bool>();

			Globals::keyOpenShaderInjectorGUI = keyOpenShaderInjectorGUI;
			Globals::keyToggleShaderInjector = keyToggleShaderInjector;
			Globals::gShaderInjectorEnabled = gShaderInjectorEnabled;
			Globals::gShowShaderInjectorGUI = gShowShaderInjectorGUI;

			WriteToLogFile("ShaderInjectorIO->ReadInjectorSettings: parsed injector settings");
		}
		catch (...)
		{
			WriteToLogFileError("ShaderInjectorIO->ReadInjectorSettings: failed to parse injector settings");
			return false;
		}

		return true;
	}

	void CreateInjectorSettings() 
	{
		std::string injectorSettingsPath = GetInjectorSettingsPath();

		if (FileExists(injectorSettingsPath))
		{
			WriteToLogFileWarning("ShaderInjectorIO->CreateInjectorSettings: injector settings int already exists!");
			return;
		}

		ini::IniFile injectorSettingsINI;
		injectorSettingsINI["InjectorSettings"]["OpenMenuKey"] = Globals::keyOpenShaderInjectorGUI;
		injectorSettingsINI["InjectorSettings"]["ToggleInjectorKey"] = Globals::keyToggleShaderInjector;
		injectorSettingsINI["InjectorSettings"]["InjectorEnabled"] = Globals::gShaderInjectorEnabled;
		injectorSettingsINI["InjectorSettings"]["MenuOpen"] = Globals::gShowShaderInjectorGUI;

		std::ofstream file(PathFromUtf8(injectorSettingsPath), std::ios::out | std::ios::trunc);

		if (!file.is_open())
		{
			WriteToLogFileError("ShaderInjectorIO->CreateInjectorSettings: failed to open injector settings: " + injectorSettingsPath);
			return;
		}

		injectorSettingsINI.encode(file);

		file.close();

		WriteToLogFile("ShaderInjectorIO->CreateInjectorSettings: new injector settings created. ");
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INITALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INITALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INITALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	bool Initialize()
	{
		WriteToLogFile("ShaderInjectorIO->Initialize: Initalizing...");

		std::string shaderInjectorDirectory = GetShaderInjectorDirectory();
		std::string logsDirectory = GetLogsDirectory();
		std::string toolsDirectory = GetToolsDirectory();
		std::string dumpDirectory = GetDumpsDirectory();
		std::string uncapturedPSODirectory = GetUncapturedPSODirectory();
		std::string shaderReplacementsDirectory = GetShaderReplacementsDirectory();
		std::string shaderSourcesDirectory = GetShaderSourcesDirectory();
		std::string injectorSettingsPath = GetInjectorSettingsPath();

		//========================= INJECTOR SETTINGS =========================
		//start by reading (or creating) injector settings to configure the injector

		bool injectorSettingsReadResult = ReadInjectorSettings();

		//check if there is existing injetor settings
		if (!injectorSettingsReadResult)
		{
			if(!FileExists(injectorSettingsPath))
				CreateInjectorSettings();
		}

		//========================= CREATE DIRECTORY STRUCTURE =========================
		//if the directory structure or folders do not exist... that will become a problem later...
		//but just as a helpful measure we will go ahead and build it out

		if (!DirectoryExists(shaderInjectorDirectory))
		{
			DirectoryCreate(shaderInjectorDirectory);
			WriteToLogFileWarning("ShaderInjectorIO->Initalize: " + shaderInjectorDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(logsDirectory))
		{
			DirectoryCreate(logsDirectory);
			WriteToLogFile("ShaderInjectorIO->Initalize: " + logsDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(toolsDirectory))
		{
			DirectoryCreate(toolsDirectory);
			WriteToLogFileWarning("ShaderInjectorIO->Initalize: " + toolsDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(dumpDirectory))
		{
			DirectoryCreate(dumpDirectory);
			WriteToLogFile("ShaderInjectorIO->Initalize: " + dumpDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(uncapturedPSODirectory))
		{
			DirectoryCreate(uncapturedPSODirectory);
			WriteToLogFile("ShaderInjectorIO->Initalize: " + uncapturedPSODirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(shaderReplacementsDirectory))
		{
			DirectoryCreate(shaderReplacementsDirectory);
			WriteToLogFile("ShaderInjectorIO->Initalize: " + shaderReplacementsDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(shaderSourcesDirectory))
		{
			DirectoryCreate(shaderSourcesDirectory);
			WriteToLogFileWarning("ShaderInjectorIO->Initalize: " + shaderSourcesDirectory + " did not exist! Created anyway.");
		}

		//create sudirectories within shader injector sources for the different shader types that we can potentially create
		const char* shaderSourceSubdirectories[] =
		{
			"VertexShaders",
			"HullShaders",
			"DomainShaders",
			"GeometryShaders",
			"PixelShaders",
			"ComputeShaders",
		};

		for (const char* shaderSourceSubdirectory : shaderSourceSubdirectories)
		{
			std::string shaderSourceDirectory = GetShaderSourcesDirectory(shaderSourceSubdirectory);

			if (!DirectoryExists(shaderSourceDirectory))
			{
				DirectoryCreate(shaderSourceDirectory);
				WriteToLogFileWarning("ShaderInjectorIO->Initalize: " + shaderSourceDirectory + " did not exist! Created anyway.");
			}
		}

		//========================= CREATE INTERNAL SHADER FILES =========================
		//now go ahead and create the internal shader files that the injector uses to mark shaders
		//NOTE TO SELF 1: we should make this a configuration as doing this every time we boot up will slow startup times
		//NOTE TO SELF 2: also do file checks where if the file does not exist, that is when we force remake them
		//NOTE TO SELF 3: if earlier we built a fresh directory/folder structure, then we won't have dxc.exe so compiling these will fail

		WriteInternalMarkerPixelShaderSourceCodeToDisk();
		WriteInternalNullPixelShaderSourceCodeToDisk();
		WriteInternalMarkerComputeShaderSourceCodeToDisk();

		std::string internalMarkerPixelShaderBlobPath = GetInternalMarkerPixelShaderBlobFilePath();
		std::string internalMarkerComputeShaderBlobPath = GetInternalMarkerComputeShaderBlobFilePath();

		bool internalMarkerPixelShaderBlobResult = CompileSourceToDXILBlob(
			GetInternalMarkerPixelShaderSourceCodeFilePath(),
			"ps_6_6", //target profile, for rebirth ps_6_6 is what I found in renderdoc
			"main", //name of the function within the shader to execute
			internalMarkerPixelShaderBlobPath); //output blob file path

		bool internalMarkerComputeShaderBlobResult = CompileSourceToDXILBlob(
			GetInternalMarkerComputeShaderSourceCodeFilePath(),
			"cs_6_6", //target profile, for rebirth cs_6_6 is what I found in renderdoc
			"main", //name of the function within the shader to execute
			internalMarkerComputeShaderBlobPath); //output blob file path

		return true;
	}
}
