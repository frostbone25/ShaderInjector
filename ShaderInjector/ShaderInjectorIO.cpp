// shaderinjector_io.cpp

#include "ShaderInjectorIO.h"

#include <Windows.h>
#include <io.h>
#include <vector>
#include <fstream>
#include <cstring>

#include "ShaderTemplates.h"

namespace ShaderInjectorIO
{
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	bool PathExists(const std::string& path)
	{
		DWORD attributes = GetFileAttributesA(path.c_str());

		return attributes != INVALID_FILE_ATTRIBUTES;
	}

	bool FileExists(const std::string& path)
	{
		DWORD attributes = GetFileAttributesA(path.c_str());

		if (attributes == INVALID_FILE_ATTRIBUTES)
			return false;

		return !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	void DeleteFileIfExists(const std::string& path)
	{
		if (!path.empty() && ShaderInjectorIO::FileExists(path))
			DeleteFileA(path.c_str());
	}

	bool WriteBinaryFile(const std::string& path, const void* data, size_t size)
	{
		if (!data || size == 0)
			return false;

		FILE* f = nullptr;
		fopen_s(&f, path.c_str(), "wb");

		if (!f)
			return false;

		const size_t written = fwrite(data, 1, size, f);
		fclose(f);

		return written == size;
	}

	bool WriteTextFileIfMissing(const std::string& path, const std::string& text)
	{
		if (FileExists(path))
			return true;

		std::ofstream file(path, std::ios::out | std::ios::trunc);

		if (!file.is_open())
			return false;

		file << text;
		return !file.fail();
	}

	bool DirectoryExists(const std::string& path)
	{
		DWORD attributes = GetFileAttributesA(path.c_str());

		if (attributes == INVALID_FILE_ATTRIBUTES)
			return false;

		return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	void DirectoryCreate(const std::string& path)
	{
		CreateDirectoryA(path.c_str(), nullptr);
	}

	std::string DirectoryFromPath(const std::string& path)
	{
		const size_t slash = path.find_last_of("\\/");
		return slash == std::string::npos ? "" : path.substr(0, slash);
	}

	std::string FileNameFromPath(const std::string& path)
	{
		const size_t slash = path.find_last_of("\\/");
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	bool IsAbsolutePath(const std::string& path)
	{
		if (path.size() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
			return true;

		return path.size() >= 2 && ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/'));
	}

	void CollectFilesByExtension(
		const std::string& directory,
		const std::string& extension,
		std::vector<std::string>& outFiles,
		bool recursive,
		bool includeFullPath)
	{
		if (directory.empty() || extension.empty())
			return;

		WIN32_FIND_DATAA findData{};
		const std::string filePattern = directory + "\\*" + extension;
		HANDLE findHandle = FindFirstFileA(filePattern.c_str(), &findData);

		if (findHandle != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					outFiles.push_back(includeFullPath ? directory + "\\" + findData.cFileName : findData.cFileName);
			} while (FindNextFileA(findHandle, &findData));

			FindClose(findHandle);
		}

		if (!recursive)
			return;

		const std::string childPattern = directory + "\\*";
		findHandle = FindFirstFileA(childPattern.c_str(), &findData);

		if (findHandle == INVALID_HANDLE_VALUE)
			return;

		do
		{
			if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				continue;

			if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
				continue;

			CollectFilesByExtension(directory + "\\" + findData.cFileName, extension, outFiles, true, includeFullPath);
		} while (FindNextFileA(findHandle, &findData));

		FindClose(findHandle);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DIRECTORIES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DIRECTORIES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DIRECTORIES |||||||||||||||||||||||||||||||||||||||||||||||||||||

	std::string GetGameDirectory()
	{
		char path[MAX_PATH] = {};

		GetModuleFileNameA(nullptr, path, MAX_PATH);

		char* lastSlash = strrchr(path, '\\');

		if (lastSlash)
			*lastSlash = '\0';

		return path;
	}

	std::string GetShaderInjectorDirectory()
	{
		return GetGameDirectory() + "\\ShaderInjector";
	}

	std::string GetLogsDirectory()
	{
		return GetShaderInjectorDirectory() + "\\Logs";
	}

	std::string GetLogFilePath()
	{
		return GetLogsDirectory() + "\\ShaderInjector" + extensionLOG;
	}

	std::string GetDumpsDirectory()
	{
		return GetShaderInjectorDirectory() + "\\Dumps";
	}

	std::string GetUncapturedPSODirectory()
	{
		return GetShaderInjectorDirectory() + "\\UncapturedPSOs";
	}

	std::string GetToolsDirectory()
	{
		return GetShaderInjectorDirectory() + "\\Tools";
	}

	std::string GetToolPathDXC()
	{
		return GetToolsDirectory() + "\\dxc" + extensionEXE;
	}

	std::string GetShaderReplacementsDirectory()
	{
		return GetShaderInjectorDirectory() + "\\ShaderReplacements";
	}

	std::string GetShaderSourcesDirectory()
	{
		return GetShaderInjectorDirectory() + "\\ShaderSources";
	}

	std::string GetShaderSourcesDirectory(const std::string& shaderTypeDirectoryName)
	{
		return GetShaderSourcesDirectory() + "\\" + shaderTypeDirectoryName;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//TODO: a current and a previous log file

	//clears log file contents
	void PurgeLogFile()
	{
		FILE* f = nullptr;

		fopen_s(&f, GetLogFilePath().c_str(), "w");

		if (f)
			fclose(f);
	}

	//appends text to a log file
	//NOTE: if there is no log file, one is created
	void WriteToLogFile(const std::string& text)
	{
		FILE* f = nullptr;

		fopen_s(&f, GetLogFilePath().c_str(), "a");

		if (!f)
			return;

		//write text by itself in log file
		//fprintf(f, "%s\n", text.c_str());

		SYSTEMTIME systemTime;
		GetLocalTime(&systemTime);

		//write text with timestamp appended before text.
		fprintf(
			f,
			"[%02d:%02d:%02d] %s\n",
			systemTime.wHour,
			systemTime.wMinute,
			systemTime.wSecond,
			text.c_str());

		fclose(f);
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

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| TOOLS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| TOOLS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| TOOLS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	bool RunProcess(const std::string& commandLine)
	{
		STARTUPINFOA startupInformation{};
		PROCESS_INFORMATION processInformation{};

		startupInformation.cb = sizeof(startupInformation);

		char cmd[4096];
		strcpy_s(cmd, commandLine.c_str());

		BOOL ok = CreateProcessA(
			nullptr,
			cmd,
			nullptr,
			nullptr,
			FALSE,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&startupInformation,
			&processInformation);

		if (!ok)
			return false;

		WaitForSingleObject(processInformation.hProcess, INFINITE);

		DWORD exitCode = 0;
		GetExitCodeProcess(processInformation.hProcess, &exitCode);

		CloseHandle(processInformation.hThread);
		CloseHandle(processInformation.hProcess);

		return exitCode == 0;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||

	bool GenerateShaderTextDXIL(const std::string shaderBytecodeFilePath)
	{
		if (!FileExists(shaderBytecodeFilePath))
		{
			WriteToLogFile("[ShaderInjectorIO] GenerateShaderTextDXIL() error! shader bytecode file not found! " + shaderBytecodeFilePath);
			return false;
		}

		std::string dxcPath = GetToolPathDXC();

		if (!FileExists(dxcPath))
		{
			WriteToLogFile("[ShaderInjectorIO] GenerateShaderTextDXIL() error! dxc.exe was not found!");
			return false;
		}

		std::string txtPath = shaderBytecodeFilePath.substr(0, shaderBytecodeFilePath.size() - 4) + ".dxil.txt";
		std::string command =
			"cmd.exe /C \"\"" +
			dxcPath +
			"\" -dumpbin \"" +
			shaderBytecodeFilePath +
			"\" > \"" +
			txtPath +
			"\"\"";

		char cmd[4096];
		strcpy_s(cmd, command.c_str());

		//NOTE TO SELF: command verification
		//MessageBoxA(nullptr, cmd, "DXC Command", MB_OK);

		RunProcess(cmd);

		return true;
	}

	bool DumpShaderBytecode(const void* bytecode, size_t size, uint64_t hash, const std::string namePrefix, const std::string& directory)
	{
		if (!bytecode || size == 0)
		{
			WriteToLogFile("[ShaderInjectorIO] DumpShaderBytecode() error! given shader bytecode is null or size is 0!");
			return false;
		}

		char filename[256];
		sprintf_s(filename, "%016llX.bin", (unsigned long long)hash);
		std::string path = directory + "\\" + namePrefix + "_" + filename;

		FILE* f = nullptr;

		fopen_s(&f, path.c_str(), "wb");

		if (!f)
			return false;

		fwrite(bytecode, 1, size, f);
		fclose(f);

		return GenerateShaderTextDXIL(path);
	}

	bool CompileSourceToDXILBlob(
		const std::string& shaderSourceFilePath, //NOTE: this NEEDS to be an .hlsl source shader text file
		const std::string& shaderProfile, //target profile, for rebirth ps_6_6 is what I found in renderdoc
		const std::string& entryPoint, //name of the function within the shader to execute
		std::string& outBlobPath) //output blob file path
	{
		if (!FileExists(shaderSourceFilePath))
		{
			WriteToLogFile("[ShaderInjectorIO] CompileSourceToDXILBlob() error! shader source file not found! " + shaderSourceFilePath);
			return false;
		}

		std::string dxcPath = GetToolPathDXC();

		if (!FileExists(dxcPath))
		{
			WriteToLogFile("[ShaderInjectorIO] CompileSourceToDXILBlob() error! dxc.exe was not found!");
			return false;
		}

		if (outBlobPath.empty())
			outBlobPath = shaderSourceFilePath.substr(0, shaderSourceFilePath.find_last_of('.')) + ".blob";

		char cmd[4096];

		//cmd.exe /C {dxc.exe path} -T {shader profile} -E {entry point} {shader source path} -Fo {shader output path.blob}

		sprintf_s(
			cmd,
			"cmd.exe /C \"\"%s\" -T %s -E %s \"%s\" -Fo \"%s\"\"",
			dxcPath.c_str(),
			shaderProfile.c_str(),
			entryPoint.c_str(),
			shaderSourceFilePath.c_str(),
			outBlobPath.c_str());

		//MessageBoxA(nullptr, cmd, "DXC Compile Command", MB_OK);

		return RunProcess(cmd);
	}

	bool LoadDXILBlobFromDisk(
		const std::string& shaderBlobFilePath, //NOTE: this NEEDS to be a DXIL compiled binary file
		std::vector<uint8_t>& outBlob) //array to contain the data in memory
	{
		if (!FileExists(shaderBlobFilePath))
		{
			WriteToLogFile("[ShaderInjectorIO] LoadDXILBlobFromDisk() error! shader blob file not found! " + shaderBlobFilePath);
			return false;
		}

		//purge blob so we ensure that we are starting clean when we copy the data into it
		outBlob.clear();

		FILE* f = nullptr;

		fopen_s(&f, shaderBlobFilePath.c_str(), "rb");

		if (!f)
			return false;

		fseek(f, 0, SEEK_END);

		long fileSize = ftell(f);

		fseek(f, 0, SEEK_SET);

		if (fileSize <= 0)
		{
			fclose(f);
			return false;
		}

		outBlob.resize((size_t)fileSize);

		fread(outBlob.data(), 1, (size_t)fileSize, f);

		fclose(f);

		return true;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||

	std::string GetInternalMarkerPixelShaderSourceCodeFilePath() 
	{
		return GetShaderInjectorDirectory() + "\\" + internalMarkerPixelShaderName + extensionHLSL;
	}

	std::string GetInternalMarkerPixelShaderBlobFilePath()
	{
		return GetShaderInjectorDirectory() + "\\" + internalMarkerPixelShaderName + extensionBLOB;
	}

	std::string GetInternalNullPixelShaderSourceCodeFilePath()
	{
		return GetShaderInjectorDirectory() + "\\" + internalNullPixelShaderName + extensionHLSL;
	}

	std::string GetInternalNullPixelShaderBlobFilePath()
	{
		return GetShaderInjectorDirectory() + "\\" + internalNullPixelShaderName + extensionBLOB;
	}

	std::string GetInternalMarkerComputeShaderSourceCodeFilePath()
	{
		return GetShaderInjectorDirectory() + "\\" + internalMarkerComputeShaderName + extensionHLSL;
	}

	std::string GetInternalMarkerComputeShaderBlobFilePath()
	{
		return GetShaderInjectorDirectory() + "\\" + internalMarkerComputeShaderName + extensionBLOB;
	}

	bool WriteInternalShaderSourceCodeToDisk(std::string shaderSourceFilePath, const char* shaderSourceCode)
	{
		//NOTE: we open with trunc so we overwrite what was originally there in the file
		//for "internal" shaders used by the shader injector, want to make sure that we are starting clean and fresh
		//as it's very possible that it got tampered with
		std::ofstream file(shaderSourceFilePath, std::ios::out | std::ios::trunc);

		if (!file.is_open())
		{
			WriteToLogFile("WriteInternalShaderCodeToDisk: failed to open file: " + shaderSourceFilePath);
			return false;
		}

		file << shaderSourceCode;

		if (file.fail())
		{
			WriteToLogFile("WriteInternalShaderCodeToDisk: failed to write file: " + shaderSourceFilePath);
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

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INITALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INITALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INITALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	bool Initialize()
	{
		WriteToLogFile("[ShaderInjectorIO] Initalizing...");

		std::string shaderInjectorDirectory = GetShaderInjectorDirectory();
		std::string logsDirectory = GetLogsDirectory();
		std::string toolsDirectory = GetToolsDirectory();
		std::string dumpDirectory = GetDumpsDirectory();
		std::string uncapturedPSODirectory = GetUncapturedPSODirectory();
		std::string shaderReplacementsDirectory = GetShaderReplacementsDirectory();
		std::string shaderSourcesDirectory = GetShaderSourcesDirectory();

		//========================= CREATE DIRECTORY STRUCTURE =========================
		//if the directory structure or folders do not exist... that will become a problem later...
		//but just as a helpful measure we will go ahead and build it out

		if (!DirectoryExists(shaderInjectorDirectory))
		{
			DirectoryCreate(shaderInjectorDirectory);
			WriteToLogFile("[ShaderInjectorIO] Initalize() | " + shaderInjectorDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(logsDirectory))
		{
			DirectoryCreate(logsDirectory);
			WriteToLogFile("[ShaderInjectorIO] Initalize() | " + logsDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(toolsDirectory))
		{
			DirectoryCreate(toolsDirectory);
			WriteToLogFile("[ShaderInjectorIO] Initalize() | " + toolsDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(dumpDirectory))
		{
			DirectoryCreate(dumpDirectory);
			WriteToLogFile("[ShaderInjectorIO] Initalize() | " + dumpDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(uncapturedPSODirectory))
		{
			DirectoryCreate(uncapturedPSODirectory);
			WriteToLogFile("[ShaderInjectorIO] Initalize() | " + uncapturedPSODirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(shaderReplacementsDirectory))
		{
			DirectoryCreate(shaderReplacementsDirectory);
			WriteToLogFile("[ShaderInjectorIO] Initalize() | " + shaderReplacementsDirectory + " did not exist! Created anyway.");
		}

		if (!DirectoryExists(shaderSourcesDirectory))
		{
			DirectoryCreate(shaderSourcesDirectory);
			WriteToLogFile("[ShaderInjectorIO] Initalize() | " + shaderSourcesDirectory + " did not exist! Created anyway.");
		}

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
				WriteToLogFile("[ShaderInjectorIO] Initalize() | " + shaderSourceDirectory + " did not exist! Created anyway.");
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
