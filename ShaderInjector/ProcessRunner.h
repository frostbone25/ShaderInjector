// ProcessRunner.h
#pragma once

#include <string>
#include <vector>

namespace ProcessRunner
{
	struct ProcessResult
	{
		bool launched = false;
		int exitCode = -1;
		std::string errorMessage;

		bool Succeeded() const
		{
			return launched && exitCode == 0;
		}
	};

	//launches an executable directly without a command shell.
	//arguments are passed individually so paths and user-controlled values never need shell escaping.
	ProcessResult Run(const std::string& executablePath, const std::vector<std::string>& arguments, const std::string& standardOutputPath = "");

	std::string GetEnvironmentVariable(const std::string& variableName);
	std::string GetCurrentExecutablePath();
	std::string GetLoadedModulePath(const std::string& moduleName);
	std::vector<std::string> FindProcessExecutablePaths(const std::string& executableName);
}
