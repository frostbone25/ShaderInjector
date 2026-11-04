#pragma once

#include <memory>
#include <string>

#include "ShaderConfiguration/ApplyResult.h"
#include "ShaderConfiguration/CompileConfiguration.h"
#include "ShaderConfiguration/ConfigurationProgress.h"
#include "ShaderConfiguration/DocumentDisk.h"

namespace DatabaseShaderConfigurations
{
	struct PreparedConfigurationOperation
	{
		bool applying = false;
		bool succeeded = false;
		ShaderConfiguration::DocumentDisk document;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress;
		ApplyResult applyResult;
		std::string errorMessage;
	};
} //namespace DatabaseShaderConfigurations
