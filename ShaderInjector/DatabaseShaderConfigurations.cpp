#include "DatabaseShaderConfigurations.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ShaderInjectorIO.h"

namespace DatabaseShaderConfigurations
{
	namespace
	{
		ShaderConfiguration::DocumentDisk gDocument;
		bool gLoaded = false;

		bool IsSafeRelativeSourcePath(const std::string& sourcePath)
		{
			if (sourcePath.empty() || ShaderInjectorIO::IsAbsolutePath(sourcePath))
				return false;

			std::string normalizedPath = sourcePath;
			std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

			size_t componentStart = 0;
			while (componentStart <= normalizedPath.size())
			{
				const size_t separator = normalizedPath.find('/', componentStart);
				const std::string component = normalizedPath.substr(
					componentStart,
					separator == std::string::npos
						? std::string::npos
						: separator - componentStart);

				if (component == "..")
					return false;

				if (separator == std::string::npos)
					break;
				componentStart = separator + 1;
			}

			return true;
		}

		bool DocumentSourcesExist(const ShaderConfiguration::DocumentDisk& document)
		{
			const std::string modifiedShadersDirectory = ShaderInjectorIO::GetModifiedShadersDirectory();
			for (const ShaderConfiguration::PropertyDisk& property : document.properties)
			{
				if (!IsSafeRelativeSourcePath(property.sourcePath) ||
					!ShaderInjectorIO::FileExists(
						ShaderInjectorIO::JoinPath(modifiedShadersDirectory, property.sourcePath)))
				{
					return false;
				}
			}

			return true;
		}

		void SortPropertiesBySource(ShaderConfiguration::DocumentDisk& document)
		{
			std::stable_sort(
				document.properties.begin(),
				document.properties.end(),
				[](const ShaderConfiguration::PropertyDisk& left,
					const ShaderConfiguration::PropertyDisk& right)
				{
					if (left.sourcePath != right.sourcePath)
						return left.sourcePath < right.sourcePath;
					return left.sourceOrder < right.sourceOrder;
				});
		}
	}

	void EnsureLoaded()
	{
		if (gLoaded)
			return;

		gLoaded = true;
		const std::string configurationsPath = ShaderInjectorIO::GetShaderConfigurationsPath();

		if (ShaderConfiguration::LoadJson(configurationsPath, gDocument) &&
			DocumentSourcesExist(gDocument))
		{
			SortPropertiesBySource(gDocument);
			return;
		}

		ReloadProperties();
	}

	bool ReloadProperties()
	{
		const std::string modifiedShadersDirectory = ShaderInjectorIO::GetModifiedShadersDirectory();
		ShaderInjectorIO::DirectoryCreate(modifiedShadersDirectory);

		std::vector<std::string> shaderSourcePaths;
		ShaderInjectorIO::CollectFilesByExtension(
			modifiedShadersDirectory,
			ShaderInjectorIO::extensionHLSL,
			shaderSourcePaths,
			true,
			true);
		std::sort(shaderSourcePaths.begin(), shaderSourcePaths.end());

		ShaderConfiguration::DocumentDisk refreshedDocument{};
		for (const std::string& shaderSourcePath : shaderSourcePaths)
		{
			const std::string relativeSourcePath =
				ShaderInjectorIO::MakeRelativePath(shaderSourcePath, modifiedShadersDirectory);
			if (!IsSafeRelativeSourcePath(relativeSourcePath))
			{
				ShaderInjectorIO::WriteToLogFileWarning(
					"DatabaseShaderConfigurations->ReloadProperties: skipped source outside ModifiedShaders: " +
					shaderSourcePath);
				continue;
			}

			std::string sourceText;
			if (!ShaderInjectorIO::ReadTextFile(shaderSourcePath, sourceText))
			{
				ShaderInjectorIO::WriteToLogFileWarning(
					"DatabaseShaderConfigurations->ReloadProperties: failed to read " +
					shaderSourcePath);
				continue;
			}

			ShaderConfiguration::ParseShaderSource(
				relativeSourcePath,
				sourceText,
				refreshedDocument.properties);
		}

		SortPropertiesBySource(refreshedDocument);

		if (!ShaderConfiguration::WriteJson(
			ShaderInjectorIO::GetShaderConfigurationsPath(),
			refreshedDocument))
		{
			ShaderInjectorIO::WriteToLogFileError(
				"DatabaseShaderConfigurations->ReloadProperties: failed to write ShaderConfigurations.json");
			return false;
		}

		gDocument = std::move(refreshedDocument);
		gLoaded = true;
		ShaderInjectorIO::WriteToLogFile(
			"DatabaseShaderConfigurations->ReloadProperties: properties=" +
			std::to_string(gDocument.properties.size()) +
			" sources=" + std::to_string(shaderSourcePaths.size()));
		return true;
	}

	const ShaderConfiguration::DocumentDisk& GetDocument()
	{
		EnsureLoaded();
		return gDocument;
	}

	ShaderConfiguration::DocumentDisk& GetEditableDocument()
	{
		EnsureLoaded();
		return gDocument;
	}

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

		struct PendingWrite
		{
			std::string destinationPath;
			std::string temporaryPath;
			std::string sourceText;
		};

		std::vector<PendingWrite> pendingWrites;
		pendingWrites.reserve(propertiesBySource.size());

		for (const auto& sourceProperties : propertiesBySource)
		{
			const std::string sourcePath =
				ShaderInjectorIO::JoinPath(modifiedShadersDirectory, sourceProperties.first);
			std::string sourceText;
			if (!ShaderInjectorIO::ReadTextFile(sourcePath, sourceText))
			{
				result.errorMessage = "Failed to read shader source: " + sourceProperties.first;
				return result;
			}

			std::string rewrittenSource;
			std::string rewriteError;
			if (!ShaderConfiguration::RewriteShaderSource(
				sourceText,
				sourceProperties.second,
				rewrittenSource,
				rewriteError))
			{
				result.errorMessage = rewriteError;
				return result;
			}

			pendingWrites.push_back({
				sourcePath,
				sourcePath + ".shaderconfig.tmp",
				std::move(rewrittenSource) });
		}

		for (const PendingWrite& pendingWrite : pendingWrites)
		{
			if (!ShaderInjectorIO::WriteTextFile(
				pendingWrite.temporaryPath,
				pendingWrite.sourceText))
			{
				result.errorMessage = "Failed to stage shader source: " + pendingWrite.destinationPath;

				for (const PendingWrite& cleanupWrite : pendingWrites)
					ShaderInjectorIO::DeleteFileIfExists(cleanupWrite.temporaryPath);
				return result;
			}
		}

		for (const PendingWrite& pendingWrite : pendingWrites)
		{
			if (!ShaderInjectorIO::MovePath(
				pendingWrite.temporaryPath,
				pendingWrite.destinationPath,
				true))
			{
				result.errorMessage = "Failed to replace shader source: " + pendingWrite.destinationPath;

				for (const PendingWrite& cleanupWrite : pendingWrites)
					ShaderInjectorIO::DeleteFileIfExists(cleanupWrite.temporaryPath);
				return result;
			}
		}

		if (!ShaderConfiguration::WriteJson(
			ShaderInjectorIO::GetShaderConfigurationsPath(),
			gDocument))
		{
			result.errorMessage = "Shader sources were updated, but ShaderConfigurations.json could not be saved.";
			return result;
		}

		result.succeeded = true;
		result.sourceFileCount = pendingWrites.size();
		ShaderInjectorIO::WriteToLogFileSuccess(
			"DatabaseShaderConfigurations->ApplyChanges: properties=" +
			std::to_string(result.propertyCount) +
			" sources=" + std::to_string(result.sourceFileCount));
		return result;
	}
}
