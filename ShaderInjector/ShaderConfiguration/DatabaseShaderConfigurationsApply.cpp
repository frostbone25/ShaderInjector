#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/DatabaseShaderConfigurationsInternal.h"
#include "ShaderConfiguration/PendingWrite.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "IO/ShaderInjectorIO.h"

namespace DatabaseShaderConfigurations
{
	ApplyResult ApplyChanges()
	{
		EnsureLoaded();

		ApplyResult result{};
		result.propertyCount = gDocument.properties.size();
		const std::string modifiedShadersDirectory = ShaderInjectorIO::GetModifiedShadersDirectory();
		std::map<std::string, std::vector<const ShaderConfiguration::PropertyDisk*>> propertiesBySource;

		for (const ShaderConfiguration::PropertyDisk& property : gDocument.properties)
		{
			if (!IsSafeRelativeSourcePath(property.sourcePath))
			{
				result.errorMessage = "Unsafe Shader Configuration source path: " + property.sourcePath;
				return result;
			}

			propertiesBySource[property.sourcePath].push_back(&property);
		}

		std::vector<PendingWrite> pendingWrites;
		pendingWrites.reserve(propertiesBySource.size());

		for (const auto& sourceProperties : propertiesBySource)
		{
			const std::string sourcePath = ShaderInjectorIO::JoinPath(modifiedShadersDirectory, sourceProperties.first);

			std::string sourceText;

			if (!ShaderInjectorIO::ReadTextFile(sourcePath, sourceText))
			{
				result.errorMessage = "Failed to read shader source: " + sourceProperties.first;
				return result;
			}

			std::string rewrittenSource;
			std::string rewriteError;

			if (!ShaderConfiguration::RewriteShaderSource(sourceText, sourceProperties.second, rewrittenSource, rewriteError))
			{
				result.errorMessage = rewriteError;
				return result;
			}

			pendingWrites.push_back({sourcePath, sourcePath + ".shaderconfig.tmp", std::move(rewrittenSource) });
		}

		for (const PendingWrite& pendingWrite : pendingWrites)
		{
			if (!ShaderInjectorIO::WriteTextFile(pendingWrite.temporaryPath, pendingWrite.sourceText))
			{
				result.errorMessage = "Failed to stage shader source: " + pendingWrite.destinationPath;

				for (const PendingWrite& cleanupWrite : pendingWrites)
					ShaderInjectorIO::DeleteFileIfExists(cleanupWrite.temporaryPath);

				return result;
			}
		}

		for (const PendingWrite& pendingWrite : pendingWrites)
		{
			if (!ShaderInjectorIO::MovePath(pendingWrite.temporaryPath, pendingWrite.destinationPath, true))
			{
				result.errorMessage = "Failed to replace shader source: " + pendingWrite.destinationPath;

				for (const PendingWrite& cleanupWrite : pendingWrites)
					ShaderInjectorIO::DeleteFileIfExists(cleanupWrite.temporaryPath);

				return result;
			}
		}

		if (!ShaderConfiguration::WriteJson(ShaderInjectorIO::GetShaderConfigurationsPath(), gDocument))
		{
			result.errorMessage = "Shader sources were updated, but ShaderConfigurations.json could not be saved.";
			return result;
		}

		result.succeeded = true;
		result.sourceFileCount = pendingWrites.size();
		ShaderInjectorIO::WriteToLogFileSuccess("DatabaseShaderConfigurations->ApplyChanges: properties = " + std::to_string(result.propertyCount) + " sources = " + std::to_string(result.sourceFileCount));
		return result;
	}
}
