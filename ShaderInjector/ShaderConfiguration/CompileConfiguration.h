#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ShaderConfiguration/DocumentDisk.h"
#include "ShaderConfiguration/PropertyMacro.h"
#include "ShaderConfiguration/ConfigurationProgress.h"

namespace ShaderConfiguration
{
	class CompileConfiguration
	{
	private:
		std::vector<PropertyMacro> propertyMacros;
		std::vector<std::string> defines;
		std::string fingerprint;
		std::shared_ptr<ConfigurationProgress> progress;
		std::string modifiedShadersDirectory;
		std::unordered_map<std::string, std::string> sourceFingerprints;

	public:
		explicit CompileConfiguration(const DocumentDisk& document, std::shared_ptr<ConfigurationProgress> progressTracker = {});

		const std::vector<std::string>& GetDefines() const;
		const std::string& GetFingerprint() const;
		std::string GetSourceFingerprint(const std::string& shaderSourcePath) const;
		//prepare this map on a background worker before publishing the configuration
		void PrepareSourceFingerprints(
			const std::vector<std::string>& shaderSourcePaths,
			std::shared_ptr<const CompileConfiguration> previousConfiguration = {});
		std::shared_ptr<ConfigurationProgress> GetProgress() const;

		bool PrepareSource(
			const std::string& relativeSourcePath,
			const std::string& sourceText,
			std::string& outSourceText,
			std::string& outError) const;
	};
} //namespace ShaderConfiguration
