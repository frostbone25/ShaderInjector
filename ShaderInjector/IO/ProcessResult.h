#pragma once

#include <string>

namespace ShaderInjectorIO
{
	struct ProcessResult
	{
		bool processLaunched = false;
		int processExitCode = -1;
		std::string errorMessage;

		//count success only when the process launched and returned code zero.
		bool Succeeded() const
		{
			return processLaunched && processExitCode == 0;
		}
	};
}
