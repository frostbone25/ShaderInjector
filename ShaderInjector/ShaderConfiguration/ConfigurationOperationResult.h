#pragma once

#include <memory>
#include <string>

#include "ShaderConfiguration/ApplyResult.h"
#include "ShaderConfiguration/CompileConfiguration.h"

namespace DatabaseShaderConfigurations
{
	struct ConfigurationOperationResult
	{
		bool completed = false;
		bool applying = false;
		bool succeeded = false;
		bool forceRecompileAll = false;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration;
		ApplyResult applyResult;
		std::string errorMessage;
	};
} //namespace DatabaseShaderConfigurations
