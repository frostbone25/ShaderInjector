#pragma once

#include <cstddef>
#include <string>

namespace DatabaseShaderConfigurations
{
	struct ApplyResult
	{
		bool succeeded = false;
		size_t propertyCount = 0;
		size_t sourceFileCount = 0;
		std::string errorMessage;
	};
} //namespace DatabaseShaderConfigurations
