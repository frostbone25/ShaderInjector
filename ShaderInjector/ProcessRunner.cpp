// ProcessRunner.cpp
#include "ProcessRunner.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
	#include <Windows.h>
	#include <tlhelp32.h>
#else
	#include <fcntl.h>
	#include <limits.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

#include "StringHelper.h"

namespace ProcessRunner
{
	namespace
	{
#if defined(_WIN32)
		std::wstring QuoteWindowsArgument(const std::wstring& argument)
		{
			std::wstring quoted = L"\"";
			size_t backslashCount = 0;

			for (wchar_t character : argument)
			{
				if (character == L'\\')
				{
					++backslashCount;
					continue;
				}

				if (character == L'\"')
				{
					quoted.append(backslashCount * 2 + 1, L'\\');
					quoted.push_back(character);
					backslashCount = 0;
					continue;
				}

				quoted.append(backslashCount, L'\\');
				backslashCount = 0;
				quoted.push_back(character);
			}

			quoted.append(backslashCount * 2, L'\\');
			quoted.push_back(L'\"');
			return quoted;
		}

#endif
	}

	ProcessResult Run(
		const std::string& executablePath,
		const std::vector<std::string>& arguments,
		const std::string& standardOutputPath)
	{
		ProcessResult result{};

		if (executablePath.empty())
		{
			result.errorMessage = "Executable path is empty";
			return result;
		}

#if defined(_WIN32)
		const std::wstring wideExecutablePath = StringHelper::Utf8ToWide(executablePath);

		if (wideExecutablePath.empty())
		{
			result.errorMessage = "Executable path could not be converted to UTF-16";
			return result;
		}

		std::wstring commandLine = QuoteWindowsArgument(wideExecutablePath);

		for (const std::string& argument : arguments)
		{
			commandLine.push_back(L' ');
			commandLine += QuoteWindowsArgument(StringHelper::Utf8ToWide(argument));
		}

		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');

		STARTUPINFOW startupInformation{};
		PROCESS_INFORMATION processInformation{};
		startupInformation.cb = sizeof(startupInformation);

		HANDLE outputHandle = INVALID_HANDLE_VALUE;
		BOOL inheritHandles = FALSE;

		if (!standardOutputPath.empty())
		{
			SECURITY_ATTRIBUTES securityAttributes{};
			securityAttributes.nLength = sizeof(securityAttributes);
			securityAttributes.bInheritHandle = TRUE;

			outputHandle = CreateFileW(
				StringHelper::Utf8ToWide(standardOutputPath).c_str(),
				GENERIC_WRITE,
				FILE_SHARE_READ,
				&securityAttributes,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);

			if (outputHandle == INVALID_HANDLE_VALUE)
			{
				result.errorMessage = "Could not open process output file: " + StringHelper::WindowsErrorMessage(GetLastError());
				return result;
			}

			startupInformation.dwFlags |= STARTF_USESTDHANDLES;
			startupInformation.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			startupInformation.hStdOutput = outputHandle;
			startupInformation.hStdError = outputHandle;
			inheritHandles = TRUE;
		}

		const BOOL processCreated = CreateProcessW(
			wideExecutablePath.c_str(),
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			inheritHandles,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&startupInformation,
			&processInformation);

		if (outputHandle != INVALID_HANDLE_VALUE)
			CloseHandle(outputHandle);

		if (!processCreated)
		{
			result.errorMessage = StringHelper::WindowsErrorMessage(GetLastError());
			return result;
		}

		result.launched = true;
		WaitForSingleObject(processInformation.hProcess, INFINITE);

		DWORD exitCode = static_cast<DWORD>(-1);

		if (GetExitCodeProcess(processInformation.hProcess, &exitCode))
			result.exitCode = static_cast<int>(exitCode);
		else
			result.errorMessage = StringHelper::WindowsErrorMessage(GetLastError());

		CloseHandle(processInformation.hThread);
		CloseHandle(processInformation.hProcess);

#else
		const pid_t processId = fork();

		if (processId < 0)
		{
			result.errorMessage = std::strerror(errno);
			return result;
		}

		if (processId == 0)
		{
			if (!standardOutputPath.empty())
			{
				const int outputDescriptor = open(standardOutputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (outputDescriptor < 0)
					_exit(126);

				dup2(outputDescriptor, STDOUT_FILENO);
				dup2(outputDescriptor, STDERR_FILENO);
				close(outputDescriptor);
			}

			std::vector<char*> argumentPointers;
			argumentPointers.reserve(arguments.size() + 2);
			argumentPointers.push_back(const_cast<char*>(executablePath.c_str()));

			for (const std::string& argument : arguments)
				argumentPointers.push_back(const_cast<char*>(argument.c_str()));

			argumentPointers.push_back(nullptr);

			execv(executablePath.c_str(), argumentPointers.data());
			_exit(errno == ENOENT ? 127 : 126);
		}

		result.launched = true;
		int processStatus = 0;

		while (waitpid(processId, &processStatus, 0) < 0)
		{
			if (errno == EINTR)
				continue;

			result.errorMessage = std::strerror(errno);
			return result;
		}

		if (WIFEXITED(processStatus))
			result.exitCode = WEXITSTATUS(processStatus);
		else if (WIFSIGNALED(processStatus))
			result.exitCode = 128 + WTERMSIG(processStatus);
#endif

		if (result.launched && result.exitCode != 0 && result.errorMessage.empty())
			result.errorMessage = "Process exited with code " + std::to_string(result.exitCode);

		return result;
	}

	std::string GetEnvironmentVariable(const std::string& variableName)
	{
		if (variableName.empty())
			return {};

#if defined(_WIN32)
		const std::wstring wideVariableName = StringHelper::Utf8ToWide(variableName, false);

		if (wideVariableName.empty())
			return {};

		const DWORD requiredCharacters = ::GetEnvironmentVariableW(wideVariableName.c_str(), nullptr, 0);

		if (requiredCharacters == 0)
			return {};

		std::vector<wchar_t> value(requiredCharacters, L'\0');

		if (::GetEnvironmentVariableW(wideVariableName.c_str(), value.data(), requiredCharacters) == 0)
			return {};

		return StringHelper::WideToUtf8(value.data());
#else
		const char* value = std::getenv(variableName.c_str());
		return value ? value : "";
#endif
	}

	std::string GetCurrentExecutablePath()
	{
#if defined(_WIN32)
		std::vector<wchar_t> pathBuffer(1024, L'\0');

		for (;;)
		{
			const DWORD pathLength = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));

			if (pathLength == 0)
				return {};

			if (pathLength < pathBuffer.size() - 1)
				return StringHelper::WideToUtf8(std::wstring(pathBuffer.data(), pathLength));

			pathBuffer.resize(pathBuffer.size() * 2);
		}
#else
		std::vector<char> pathBuffer(PATH_MAX + 1, '\0');
		const ssize_t pathLength = readlink("/proc/self/exe", pathBuffer.data(), PATH_MAX);
		return pathLength > 0 ? std::string(pathBuffer.data(), static_cast<size_t>(pathLength)) : std::string();
#endif
	}

	std::string GetLoadedModulePath(const std::string& moduleName)
	{
#if defined(_WIN32)
		const std::wstring wideModuleName = StringHelper::Utf8ToWide(moduleName, false);
		HMODULE module = wideModuleName.empty() ? nullptr : GetModuleHandleW(wideModuleName.c_str());

		if (!module)
			return {};

		std::vector<wchar_t> pathBuffer(32768, L'\0');
		const DWORD pathLength = GetModuleFileNameW(module, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));

		return pathLength > 0
			? StringHelper::WideToUtf8(std::wstring(pathBuffer.data(), pathLength))
			: std::string();
#else
		(void)moduleName;
		return {};
#endif
	}

	std::vector<std::string> FindProcessExecutablePaths(const std::string& executableName)
	{
		std::vector<std::string> executablePaths;

		if (executableName.empty())
			return executablePaths;

#if defined(_WIN32)
		const HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

		if (processSnapshot == INVALID_HANDLE_VALUE)
			return executablePaths;

		PROCESSENTRY32W processEntry{};
		processEntry.dwSize = sizeof(processEntry);

		if (Process32FirstW(processSnapshot, &processEntry))
		{
			do
			{
				if (!StringHelper::EqualsIgnoreCase(StringHelper::WideToUtf8(processEntry.szExeFile), executableName))
					continue;

				const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processEntry.th32ProcessID);
				if (!process)
					continue;

				std::vector<wchar_t> executablePath(32768, L'\0');
				DWORD executablePathLength = static_cast<DWORD>(executablePath.size());

				if (QueryFullProcessImageNameW(process, 0, executablePath.data(), &executablePathLength))
				{
					executablePaths.push_back(
						StringHelper::WideToUtf8(std::wstring(executablePath.data(), executablePathLength)));
				}

				CloseHandle(process);
			} while (Process32NextW(processSnapshot, &processEntry));
		}

		CloseHandle(processSnapshot);
#else
		namespace FileSystem = std::filesystem;
		std::error_code error;

		for (const FileSystem::directory_entry& processDirectory : FileSystem::directory_iterator("/proc", error))
		{
			if (error)
				break;

			const std::string processId = processDirectory.path().filename().string();

			if (processId.empty() || !std::all_of(processId.begin(), processId.end(), ::isdigit))
				continue;

			const FileSystem::path executablePath = FileSystem::read_symlink(processDirectory.path() / "exe", error);

			if (!error && StringHelper::EqualsIgnoreCase(executablePath.filename().string(), executableName))
				executablePaths.push_back(executablePath.string());

			error.clear();
		}
#endif

		return executablePaths;
	}
}
