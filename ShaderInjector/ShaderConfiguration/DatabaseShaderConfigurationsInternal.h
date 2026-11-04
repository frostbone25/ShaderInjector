#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ShaderConfiguration/CompileConfiguration.h"
#include "ShaderConfiguration/DocumentDisk.h"
#include "ShaderConfiguration/PreparedConfigurationOperation.h"
#include "ShaderConfiguration/PresetDisk.h"

namespace DatabaseShaderConfigurations
{
	extern ShaderConfiguration::DocumentDisk gDocument;
	extern ShaderConfiguration::DocumentDisk gAppliedDocument;
	extern std::shared_ptr<const ShaderConfiguration::CompileConfiguration> gAppliedConfiguration;
	extern std::recursive_mutex gConfigurationMutex;
	extern bool gHasAppliedDocument;
	extern bool gLoaded;
	extern std::vector<ShaderConfiguration::PresetDisk> gPresets;
	bool IsSafeRelativeSourcePath(const std::string& sourcePath);
	PreparedConfigurationOperation PrepareApplyDocument(
		ShaderConfiguration::DocumentDisk document,
		const std::string& modifiedShadersDirectory,
		const std::string& configurationsPath,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress);
	PreparedConfigurationOperation PrepareReloadDocument(
		ShaderConfiguration::DocumentDisk previousAppliedDocument,
		bool hasPreviousAppliedDocument,
		const std::string& modifiedShadersDirectory,
		const std::string& configurationsPath,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress);
} //namespace DatabaseShaderConfigurations
