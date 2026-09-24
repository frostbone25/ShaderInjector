#include "ShaderInjectorIO.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "StringHelper.h"

namespace ShaderInjectorIO
{
//windows receives one command-line string, so escape quotes and trailing slashes before joining arguments.
#if defined(_WIN32)
	static std::wstring QuoteWindowsArgument(const std::wstring& argument)
	{
		std::wstring quotedArgument = L"\"";
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
				quotedArgument.append(backslashCount * 2 + 1, L'\\');
				quotedArgument.push_back(character);
				backslashCount = 0;
				continue;
			}

			quotedArgument.append(backslashCount, L'\\');
			backslashCount = 0;
			quotedArgument.push_back(character);
		}

		quotedArgument.append(backslashCount * 2, L'\\');
		quotedArgument.push_back(L'\"');
		return quotedArgument;
	}
#endif

	ProcessResult RunProcess(const std::string& executablePath, const std::vector<std::string>& arguments, const std::string& standardOutputPath)
	{
		ProcessResult processResult{};

		if (executablePath.empty())
		{
			processResult.errorMessage = "Executable path is empty";
			return processResult;
		}

#if defined(_WIN32)
		const std::wstring wideExecutablePath = StringHelper::Utf8ToWide(executablePath);

		if (wideExecutablePath.empty())
		{
			processResult.errorMessage = "Executable path could not be converted to UTF-16";
			return processResult;
		}

		std::wstring commandLine = QuoteWindowsArgument(wideExecutablePath);

		for (const std::string& argument : arguments)
		{
			commandLine.push_back(L' ');
			commandLine += QuoteWindowsArgument(StringHelper::Utf8ToWide(argument));
		}

		std::vector<wchar_t> writableCommandLine(commandLine.begin(), commandLine.end());
		writableCommandLine.push_back(L'\0');

		STARTUPINFOW startupInformation{};
		PROCESS_INFORMATION processInformation{};
		startupInformation.cb = sizeof(startupInformation);

		HANDLE standardOutputHandle = INVALID_HANDLE_VALUE;
		BOOL inheritHandles = FALSE;

		if (!standardOutputPath.empty())
		{
			SECURITY_ATTRIBUTES securityAttributes{};
			securityAttributes.nLength = sizeof(securityAttributes);
			securityAttributes.bInheritHandle = TRUE;

			//send both streams to one file so compiler diagnostics stay together.
			standardOutputHandle = CreateFileW(
				StringHelper::Utf8ToWide(standardOutputPath).c_str(),
				GENERIC_WRITE,
				FILE_SHARE_READ,
				&securityAttributes,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);

			if (standardOutputHandle == INVALID_HANDLE_VALUE)
			{
				processResult.errorMessage = "Could not open process output file: " + StringHelper::WindowsErrorMessage(GetLastError());
				return processResult;
			}

			startupInformation.dwFlags |= STARTF_USESTDHANDLES;
			startupInformation.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			startupInformation.hStdOutput = standardOutputHandle;
			startupInformation.hStdError = standardOutputHandle;
			inheritHandles = TRUE;
		}

		const BOOL processCreated = CreateProcessW(
			wideExecutablePath.c_str(),
			writableCommandLine.data(),
			nullptr,
			nullptr,
			inheritHandles,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&startupInformation,
			&processInformation);

		if (standardOutputHandle != INVALID_HANDLE_VALUE)
			CloseHandle(standardOutputHandle);

		if (!processCreated)
		{
			processResult.errorMessage = StringHelper::WindowsErrorMessage(GetLastError());
			return processResult;
		}

		processResult.processLaunched = true;

		//wait for completion before reading the exit code and closing the process handles.
		WaitForSingleObject(processInformation.hProcess, INFINITE);

		DWORD exitCode = static_cast<DWORD>(-1);

		if (GetExitCodeProcess(processInformation.hProcess, &exitCode))
			processResult.processExitCode = static_cast<int>(exitCode);
		else
			processResult.errorMessage = StringHelper::WindowsErrorMessage(GetLastError());

		CloseHandle(processInformation.hThread);
		CloseHandle(processInformation.hProcess);
#else
		const pid_t processId = fork();

		if (processId < 0)
		{
			processResult.errorMessage = std::strerror(errno);
			return processResult;
		}

		if (processId == 0)
		{
			//after fork, this branch is the child; execv replaces it with the requested program.
			if (!standardOutputPath.empty())
			{
				const int standardOutputDescriptor = open(standardOutputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

				if (standardOutputDescriptor < 0)
					_exit(126);

				dup2(standardOutputDescriptor, STDOUT_FILENO);
				dup2(standardOutputDescriptor, STDERR_FILENO);
				close(standardOutputDescriptor);
			}

			std::vector<char*> processArguments;
			processArguments.reserve(arguments.size() + 2);
			processArguments.push_back(const_cast<char*>(executablePath.c_str()));

			for (const std::string& argument : arguments)
				processArguments.push_back(const_cast<char*>(argument.c_str()));

			processArguments.push_back(nullptr);

			execv(executablePath.c_str(), processArguments.data());

			if (errno == ENOENT)
				_exit(127);

			_exit(126);
		}

		processResult.processLaunched = true;
		//the parent waits here and retries when a signal interrupts waitpid.
		int processWaitStatus = 0;

		while (waitpid(processId, &processWaitStatus, 0) < 0)
		{
			if (errno == EINTR)
				continue;

			processResult.errorMessage = std::strerror(errno);
			return processResult;
		}

		if (WIFEXITED(processWaitStatus))
			processResult.processExitCode = WEXITSTATUS(processWaitStatus);
		else if (WIFSIGNALED(processWaitStatus))
			processResult.processExitCode = 128 + WTERMSIG(processWaitStatus);
#endif

		if (processResult.processLaunched && processResult.processExitCode != 0 && processResult.errorMessage.empty())
			processResult.errorMessage = "Process exited with code " + std::to_string(processResult.processExitCode);

		return processResult;
	}

	std::string GetEnvironmentVariable(const std::string& variableName)
	{
		if (variableName.empty())
			return {};

#if defined(_WIN32)
		const std::wstring wideVariableName = StringHelper::Utf8ToWide(variableName, false);

		if (wideVariableName.empty())
			return {};

		//query the required length first so the next call can fill an exactly sized buffer.
		const DWORD requiredCharacters = ::GetEnvironmentVariableW(wideVariableName.c_str(), nullptr, 0);

		if (requiredCharacters == 0)
			return {};

		std::vector<wchar_t> environmentCharacters(requiredCharacters, L'\0');

		if (::GetEnvironmentVariableW(wideVariableName.c_str(), environmentCharacters.data(), requiredCharacters) == 0)
			return {};

		return StringHelper::WideToUtf8(environmentCharacters.data());
#else
		const char* environmentValue = std::getenv(variableName.c_str());

		if (!environmentValue)
			return {};

		return environmentValue;
#endif
	}

	std::string GetCurrentExecutablePath()
	{
#if defined(_WIN32)
		//grow the buffer until Windows returns the full path rather than a truncated prefix.
		std::vector<wchar_t> executablePathCharacters(1024, L'\0');

		for (;;)
		{
			const DWORD executablePathLength = GetModuleFileNameW(nullptr, executablePathCharacters.data(), static_cast<DWORD>(executablePathCharacters.size()));

			if (executablePathLength == 0)
				return {};

			if (executablePathLength < executablePathCharacters.size() - 1)
				return StringHelper::WideToUtf8(std::wstring(executablePathCharacters.data(), executablePathLength));

			executablePathCharacters.resize(executablePathCharacters.size() * 2);
		}
#else
		//linux exposes the running executable through this process-specific symbolic link.
		std::vector<char> executablePathCharacters(PATH_MAX + 1, '\0');
		const ssize_t executablePathLength = readlink("/proc/self/exe", executablePathCharacters.data(), PATH_MAX);

		if (executablePathLength <= 0)
			return {};

		return std::string(executablePathCharacters.data(), static_cast<size_t>(executablePathLength));
#endif
	}

	std::string GetLoadedModulePath(const std::string& loadedModuleName)
	{
#if defined(_WIN32)
		const std::wstring wideLoadedModuleName = StringHelper::Utf8ToWide(loadedModuleName, false);

		if (wideLoadedModuleName.empty())
			return {};

		HMODULE loadedModule = GetModuleHandleW(wideLoadedModuleName.c_str());

		if (!loadedModule)
			return {};

		std::vector<wchar_t> modulePathCharacters(32768, L'\0');
		const DWORD modulePathLength = GetModuleFileNameW(loadedModule, modulePathCharacters.data(), static_cast<DWORD>(modulePathCharacters.size()));

		if (modulePathLength == 0)
			return {};

		return StringHelper::WideToUtf8(std::wstring(modulePathCharacters.data(), modulePathLength));
#else
		(void)loadedModuleName;
		return {};
#endif
	}
} //namespace ShaderInjectorIO
