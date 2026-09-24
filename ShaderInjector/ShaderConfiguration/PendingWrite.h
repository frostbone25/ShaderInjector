#pragma once

#include <string>

namespace DatabaseShaderConfigurations
{
	struct PendingWrite
	{
		std::string destinationPath;
		std::string temporaryPath;
		std::string sourceText;
	};
} //namespace DatabaseShaderConfigurations
