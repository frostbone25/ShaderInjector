#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/DatabaseShaderConfigurationsInternal.h"
#include "ShaderConfiguration/ConfigurationProgress.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "RenderPass/RenderPassShaderCompileQueue.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "IO/ShaderInjectorIO.h"

namespace DatabaseShaderConfigurations
{
	namespace
	{
		std::mutex gOperationMutex;
		std::future<PreparedConfigurationOperation> gOperationFuture;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> gOperationProgress;
		bool gOperationForceRecompileAll = false;

		void ReportPreparation(
			const std::shared_ptr<ShaderConfiguration::ConfigurationProgress>& progress,
			size_t completedTaskCount,
			size_t totalTaskCount,
			const std::string& stage)
		{
			if (progress)
				progress->ReportPreparation(completedTaskCount, totalTaskCount, stage);
		}

		void FinishOperationWithError(
			PreparedConfigurationOperation& operation,
			const std::string& errorMessage)
		{
			operation.errorMessage = errorMessage;
			operation.applyResult.errorMessage = errorMessage;
			operation.applyResult.succeeded = false;
			operation.succeeded = false;
			if (operation.progress)
				operation.progress->FinishWithError(errorMessage);
		}

		ShaderConfiguration::PresetDisk MakePreset(const ShaderConfiguration::DocumentDisk& document)
		{
			ShaderConfiguration::PresetDisk preset{};
			preset.id = document.presetID;
			preset.name = document.presetID;
			const std::string presetPath = ShaderInjectorIO::JoinPath(
				ShaderInjectorIO::GetShaderConfigurationPresetsDirectory(),
				ShaderConfiguration::GetPresetFileName(preset.id));
			ShaderConfiguration::PresetDisk savedPreset;
			if (ShaderConfiguration::LoadPresetJson(presetPath, savedPreset) && savedPreset.id == preset.id)
			{
				preset.name = savedPreset.name;
				preset.values = std::move(savedPreset.values);
			}

			for (const ShaderConfiguration::PropertyDisk& property : document.properties)
				preset.values[property.id] = property.value;

			return preset;
		}

		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> BuildCompileConfiguration(
			const ShaderConfiguration::DocumentDisk& document,
			const std::string& modifiedShadersDirectory,
			const std::shared_ptr<ShaderConfiguration::ConfigurationProgress>& progress,
			bool prepareDependencies,
			const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& previousConfiguration)
		{
			auto configuration = std::make_shared<ShaderConfiguration::CompileConfiguration>(document, progress);
			const bool shaderPropertiesUnchanged = previousConfiguration && configuration->GetFingerprint() == previousConfiguration->GetFingerprint();

			if (shaderPropertiesUnchanged)
			{
				//framework and metadata edits reuse the published identities without touching shader files.
				configuration->PrepareSourceFingerprints({}, previousConfiguration);
				return configuration;
			}

			if (!prepareDependencies)
				return configuration;

			//this helper is enabled only by the async worker, before its configuration becomes visible to rendering.
			ReportPreparation(progress, 0, 0, "Finding shader dependencies");
			std::vector<std::string> shaderSourcePaths;
			ShaderInjectorIO::CollectFilesByExtension(modifiedShadersDirectory, ShaderInjectorIO::extensionHLSL, shaderSourcePaths, true, true);
			ShaderInjectorIO::CollectFilesByExtension(ShaderInjectorIO::GetRenderPassesDirectory(), ShaderInjectorIO::extensionHLSL, shaderSourcePaths, true, true);
			std::sort(shaderSourcePaths.begin(), shaderSourcePaths.end());
			shaderSourcePaths.erase(std::unique(shaderSourcePaths.begin(), shaderSourcePaths.end()), shaderSourcePaths.end());
			configuration->PrepareSourceFingerprints(shaderSourcePaths, previousConfiguration);
			return configuration;
		}

		PreparedConfigurationOperation PrepareApplyOperation(
			ShaderConfiguration::DocumentDisk document,
			const std::string& modifiedShadersDirectory,
			const std::string& configurationsPath,
			std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress,
			bool prepareDependencies,
			const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& previousConfiguration)
		{
			PreparedConfigurationOperation operation{};
			operation.applying = true;
			operation.document = std::move(document);
			operation.progress = std::move(progress);
			operation.applyResult.propertyCount = operation.document.properties.size();
			std::map<std::string, bool> sourcePaths;
			const size_t propertyCount = operation.document.properties.size();
			const size_t total = propertyCount + 2;
			ReportPreparation(operation.progress, 0, total, "Checking shader sources");

			for (size_t propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex)
			{
				const ShaderConfiguration::PropertyDisk& property = operation.document.properties[propertyIndex];
				if (property.propertyType != ShaderConfiguration::PropertyType::Shader)
				{
					ReportPreparation(operation.progress, propertyIndex + 1, total, "Checking shader sources");
					continue;
				}

				if (!IsSafeRelativeSourcePath(property.sourcePath))
				{
					FinishOperationWithError(operation, "Unsafe Shader Configuration source path: " + property.sourcePath);
					return operation;
				}

				const std::string sourcePath = ShaderInjectorIO::JoinPath(modifiedShadersDirectory, property.sourcePath);

				//a source can own hundreds of properties; validate its file once per apply.
				if (sourcePaths.emplace(property.sourcePath, true).second && !ShaderInjectorIO::FileExists(sourcePath))
				{
					FinishOperationWithError(operation, "Shader Configuration source does not exist: " + property.sourcePath);
					return operation;
				}

				ReportPreparation(operation.progress, propertyIndex + 1, total, "Checking shader sources");
			}

			operation.applyResult.sourceFileCount = sourcePaths.size();
			ReportPreparation(operation.progress, propertyCount, total, "Building compile configuration");
			operation.configuration = BuildCompileConfiguration(operation.document, modifiedShadersDirectory, operation.progress, prepareDependencies, previousConfiguration);
			ReportPreparation(operation.progress, propertyCount + 1, total, "Saving configuration");

			ShaderConfiguration::PresetDisk preset = MakePreset(operation.document);
			const std::string presetPath = ShaderInjectorIO::JoinPath(
				ShaderInjectorIO::GetShaderConfigurationPresetsDirectory(),
				ShaderConfiguration::GetPresetFileName(preset.id));
			std::string persistenceError;
			if (!ShaderConfiguration::WriteCatalogAndPreset(configurationsPath, presetPath, operation.document, preset, persistenceError))
			{
				FinishOperationWithError(operation, persistenceError);
				return operation;
			}

			ReportPreparation(operation.progress, total, total, "Configuration prepared");
			operation.applyResult.succeeded = true;
			operation.succeeded = true;
			return operation;
		}

		PreparedConfigurationOperation PrepareReloadOperation(
			ShaderConfiguration::DocumentDisk previousAppliedDocument,
			bool hasPreviousAppliedDocument,
			const std::string& modifiedShadersDirectory,
			const std::string& configurationsPath,
			std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress,
			bool prepareDependencies,
			const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& previousConfiguration)
		{
			PreparedConfigurationOperation operation{};
			operation.progress = std::move(progress);
			ShaderInjectorIO::DirectoryCreate(modifiedShadersDirectory);
			std::vector<std::string> shaderSourcePaths;
			ShaderInjectorIO::CollectFilesByExtension(modifiedShadersDirectory, ShaderInjectorIO::extensionHLSL, shaderSourcePaths, true, true);
			std::sort(shaderSourcePaths.begin(), shaderSourcePaths.end());
			std::map<std::string, std::pair<std::string, std::string>> appliedValuesById;

			if (hasPreviousAppliedDocument)
			{
				for (const ShaderConfiguration::PropertyDisk& property : previousAppliedDocument.properties)
					appliedValuesById[property.id] = {property.valueType, property.value};
			}

			operation.document.presetID = previousAppliedDocument.presetID;
			operation.document.categories = previousAppliedDocument.categories;
			for (const ShaderConfiguration::PropertyDisk& property : previousAppliedDocument.properties)
			{
				if (property.propertyType != ShaderConfiguration::PropertyType::Shader)
					operation.document.properties.push_back(property);
			}

			const size_t total = shaderSourcePaths.size() + 2;
			ReportPreparation(operation.progress, 0, total, "Scanning shader sources");

			for (size_t sourceIndex = 0; sourceIndex < shaderSourcePaths.size(); ++sourceIndex)
			{
				const std::string& shaderSourcePath = shaderSourcePaths[sourceIndex];
				const std::string relativeSourcePath = ShaderInjectorIO::MakeRelativePath(shaderSourcePath, modifiedShadersDirectory);

				if (!IsSafeRelativeSourcePath(relativeSourcePath))
				{
					ShaderInjectorIO::WriteToLogFileWarning("DatabaseShaderConfigurations->ReloadProperties: skipped source outside ModifiedShaders: " + shaderSourcePath);
					ReportPreparation(operation.progress, sourceIndex + 1, total, "Scanning shader sources");
					continue;
				}

				std::string sourceText;

				if (!ShaderInjectorIO::ReadTextFile(shaderSourcePath, sourceText))
				{
					ShaderInjectorIO::WriteToLogFileWarning("DatabaseShaderConfigurations->ReloadProperties: failed to read " + shaderSourcePath);
					ReportPreparation(operation.progress, sourceIndex + 1, total, "Scanning shader sources");
					continue;
				}

				ShaderConfiguration::ParseShaderSource(relativeSourcePath, sourceText, operation.document.properties);
				ReportPreparation(operation.progress, sourceIndex + 1, total, "Scanning shader sources");
			}

			for (ShaderConfiguration::PropertyDisk& property : operation.document.properties)
			{
				if (property.propertyType != ShaderConfiguration::PropertyType::Shader)
					continue;
				const auto previousValue = appliedValuesById.find(property.id);

				if (previousValue != appliedValuesById.end() && previousValue->second.first == property.valueType)
					property.value = ShaderConfiguration::Internal::NormalizeValue(property.valueType, previousValue->second.second, property.defaultValue, false);
			}

			for (const ShaderConfiguration::PropertyDisk& property : operation.document.properties)
			{
				if (property.category == "Uncategorized")
					continue;
				const bool categoryExists = std::any_of(operation.document.categories.begin(), operation.document.categories.end(), [&property](const ShaderConfiguration::ShaderConfigurationCategory& category) { return category.name == property.category; });
				if (!categoryExists)
					operation.document.categories.push_back({property.category});
			}

			std::stable_sort(
				operation.document.properties.begin(),
				operation.document.properties.end(),
				[](const ShaderConfiguration::PropertyDisk& left, const ShaderConfiguration::PropertyDisk& right)
				{
					if (left.sourcePath != right.sourcePath)
						return left.sourcePath < right.sourcePath;

					return left.sourceOrder < right.sourceOrder;
				});

			const size_t sourceCount = shaderSourcePaths.size();
			ReportPreparation(operation.progress, sourceCount, total, "Building compile configuration");
			operation.configuration = BuildCompileConfiguration(operation.document, modifiedShadersDirectory, operation.progress, prepareDependencies, previousConfiguration);
			ReportPreparation(operation.progress, sourceCount + 1, total, "Saving configuration");

			ShaderConfiguration::PresetDisk preset = MakePreset(operation.document);
			const std::string presetPath = ShaderInjectorIO::JoinPath(
				ShaderInjectorIO::GetShaderConfigurationPresetsDirectory(),
				ShaderConfiguration::GetPresetFileName(preset.id));
			std::string persistenceError;
			if (!ShaderConfiguration::WriteCatalogAndPreset(configurationsPath, presetPath, operation.document, preset, persistenceError))
			{
				FinishOperationWithError(operation, persistenceError);
				return operation;
			}

			ReportPreparation(operation.progress, total, total, "Configuration prepared");
			operation.succeeded = true;
			return operation;
		}

		PreparedConfigurationOperation SafePrepareApplyOperation(
			ShaderConfiguration::DocumentDisk document,
			const std::string& modifiedShadersDirectory,
			const std::string& configurationsPath,
			std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress,
			bool prepareDependencies = false,
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration = {})
		{
			try
			{
				return PrepareApplyOperation(std::move(document), modifiedShadersDirectory, configurationsPath, progress, prepareDependencies, previousConfiguration);
			}
			catch (const std::exception& exception)
			{
				PreparedConfigurationOperation operation{};
				operation.applying = true;
				operation.progress = std::move(progress);
				FinishOperationWithError(operation, exception.what());
				return operation;
			}
			catch (...)
			{
				PreparedConfigurationOperation operation{};
				operation.applying = true;
				operation.progress = std::move(progress);
				FinishOperationWithError(operation, "Unknown error while preparing Shader Configuration apply.");
				return operation;
			}
		}

		PreparedConfigurationOperation SafePrepareReloadOperation(
			ShaderConfiguration::DocumentDisk previousAppliedDocument,
			bool hasPreviousAppliedDocument,
			const std::string& modifiedShadersDirectory,
			const std::string& configurationsPath,
			std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress,
			bool prepareDependencies = false,
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration = {})
		{
			try
			{
				return PrepareReloadOperation(
					std::move(previousAppliedDocument),
					hasPreviousAppliedDocument,
					modifiedShadersDirectory,
					configurationsPath,
					progress,
					prepareDependencies,
					previousConfiguration);
			}
			catch (const std::exception& exception)
			{
				PreparedConfigurationOperation operation{};
				operation.progress = std::move(progress);
				FinishOperationWithError(operation, exception.what());
				return operation;
			}
			catch (...)
			{
				PreparedConfigurationOperation operation{};
				operation.progress = std::move(progress);
				FinishOperationWithError(operation, "Unknown error while preparing Shader Configuration reload.");
				return operation;
			}
		}

		bool StartOperation(
			bool applying,
			bool forceRecompileAll,
			const ShaderConfiguration::DocumentDisk& document,
			const ShaderConfiguration::DocumentDisk& appliedDocument,
			bool hasAppliedDocument,
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration)
		{
			if (!CancelBackgroundCompilation())
				return false;

			std::lock_guard<std::mutex> operationLock(gOperationMutex);

			if (gOperationFuture.valid())
				return false;

			if (gOperationProgress && gOperationProgress->GetSnapshot().blocksEditing)
				return false;

			gOperationForceRecompileAll = applying && forceRecompileAll;

			const std::string modifiedShadersDirectory = ShaderInjectorIO::GetModifiedShadersDirectory();
			const std::string configurationsPath = ShaderInjectorIO::GetShaderConfigurationsPath();
			gOperationProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
			const std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress = gOperationProgress;
			if (applying)
				progress->ReportPreparation(0, document.properties.size() + 2, "Preparing configuration");
			else
				progress->ReportPreparation(0, 0, "Finding shader sources");

			try
			{
				if (applying)
				{
					gOperationFuture = std::async(
						std::launch::async,
						[document, modifiedShadersDirectory, configurationsPath, progress, previousConfiguration]() mutable
						{
							return SafePrepareApplyOperation(std::move(document), modifiedShadersDirectory, configurationsPath, progress, true, previousConfiguration);
						});
				}
				else
				{
					gOperationFuture = std::async(
						std::launch::async,
						[appliedDocument, hasAppliedDocument, modifiedShadersDirectory, configurationsPath, progress, previousConfiguration]() mutable
						{
							return SafePrepareReloadOperation(std::move(appliedDocument), hasAppliedDocument, modifiedShadersDirectory, configurationsPath, progress, true, previousConfiguration);
						});
				}
			}
			catch (...)
			{
				gOperationProgress->FinishWithError("Could not start Shader Configuration background operation.");
				return false;
			}

			return true;
		}
	}

	bool CancelBackgroundCompilation()
	{
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress;

		{
			std::lock_guard<std::mutex> operationLock(gOperationMutex);
			progress = gOperationProgress;
		}

		if (!progress)
			return true;

		const ShaderConfiguration::ConfigurationProgressSnapshot progressSnapshot = progress->GetSnapshot();

		if (progressSnapshot.finished)
			return true;

		if (progressSnapshot.blocksEditing)
			return false;

		progress->Cancel();
		DatabaseModifiedShaders::CancelConfigurationCompiles(progress);
		DatabaseRenderPasses::CancelConfigurationCompiles(progress);
		return true;
	}

	void PrepareStartupConfiguration()
	{
		EnsureLoaded();
		ShaderConfiguration::DocumentDisk document;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration;

		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			document = gAppliedDocument;
			previousConfiguration = gAppliedConfiguration;
		}

		try
		{
			//build before the startup worker loads bytecode, so source-scoped disk caches survive a restart.
			const auto configuration = BuildCompileConfiguration(document, ShaderInjectorIO::GetModifiedShadersDirectory(), {}, true, {});
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);

			if (gAppliedConfiguration == previousConfiguration)
				gAppliedConfiguration = configuration;
		}
		catch (const std::exception& exception)
		{
			ShaderInjectorIO::WriteToLogFileWarning("DatabaseShaderConfigurations->PrepareStartupConfiguration: using conservative cache identities: " + std::string(exception.what()));
		}
	}

	PreparedConfigurationOperation PrepareApplyDocument(
		ShaderConfiguration::DocumentDisk document,
		const std::string& modifiedShadersDirectory,
		const std::string& configurationsPath,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress)
	{
		return SafePrepareApplyOperation(std::move(document), modifiedShadersDirectory, configurationsPath, std::move(progress));
	}

	PreparedConfigurationOperation PrepareReloadDocument(
		ShaderConfiguration::DocumentDisk previousAppliedDocument,
		bool hasPreviousAppliedDocument,
		const std::string& modifiedShadersDirectory,
		const std::string& configurationsPath,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress)
	{
		return SafePrepareReloadOperation(
			std::move(previousAppliedDocument),
			hasPreviousAppliedDocument,
			modifiedShadersDirectory,
			configurationsPath,
			std::move(progress));
	}

	bool QueueApplyChanges(bool forceRecompileAll)
	{
		EnsureLoaded();
		ShaderConfiguration::DocumentDisk editableDocument;
		ShaderConfiguration::DocumentDisk appliedDocument;
		bool hasAppliedDocument = false;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration;

		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			editableDocument = gDocument;
			appliedDocument = gAppliedDocument;
			hasAppliedDocument = gHasAppliedDocument;
			previousConfiguration = gAppliedConfiguration;
		}

		return StartOperation(true, forceRecompileAll, editableDocument, appliedDocument, hasAppliedDocument, std::move(previousConfiguration));
	}

	bool QueueReloadProperties()
	{
		EnsureLoaded();
		ShaderConfiguration::DocumentDisk emptyDocument;
		ShaderConfiguration::DocumentDisk appliedDocument;
		bool hasAppliedDocument = false;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration;

		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			appliedDocument = gDocument;
			appliedDocument.presetID = gAppliedDocument.presetID;
			for (ShaderConfiguration::PropertyDisk& property : appliedDocument.properties)
			{
				const auto appliedProperty = std::find_if(gAppliedDocument.properties.begin(), gAppliedDocument.properties.end(), [&property](const ShaderConfiguration::PropertyDisk& candidate) { return candidate.id == property.id; });
				if (appliedProperty != gAppliedDocument.properties.end())
					property.value = appliedProperty->value;
			}
			hasAppliedDocument = gHasAppliedDocument;
			previousConfiguration = gAppliedConfiguration;
		}

		return StartOperation(false, false, emptyDocument, appliedDocument, hasAppliedDocument, std::move(previousConfiguration));
	}

	ConfigurationOperationResult ProcessCompletedOperation()
	{
		ConfigurationOperationResult operationResult{};
		PreparedConfigurationOperation preparedOperation;

		{
			std::lock_guard<std::mutex> operationLock(gOperationMutex);

			if (!gOperationFuture.valid() || gOperationFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				return operationResult;

			preparedOperation = gOperationFuture.get();
			operationResult.completed = true;
			operationResult.applying = preparedOperation.applying;
			operationResult.succeeded = preparedOperation.succeeded;
			operationResult.forceRecompileAll = gOperationForceRecompileAll;
			operationResult.applyResult = preparedOperation.applyResult;
			operationResult.errorMessage = preparedOperation.errorMessage;
		}

		if (preparedOperation.succeeded)
		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			operationResult.previousConfiguration = gAppliedConfiguration;
			gAppliedDocument = preparedOperation.document;
			gAppliedConfiguration = std::move(preparedOperation.configuration);
			gHasAppliedDocument = true;
			gLoaded = true;
			if (preparedOperation.applying)
				gDocument = preparedOperation.document;
			for (ShaderConfiguration::PresetDisk& preset : gPresets)
			{
				if (preset.id != preparedOperation.document.presetID)
					continue;
				for (const ShaderConfiguration::PropertyDisk& property : preparedOperation.document.properties)
					preset.values[property.id] = property.value;
			}

			if (!preparedOperation.applying)
				gDocument = preparedOperation.document;
		}

		return operationResult;
	}

	ShaderConfiguration::ConfigurationProgressSnapshot GetOperationProgress()
	{
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress;

		{
			std::lock_guard<std::mutex> operationLock(gOperationMutex);
			progress = gOperationProgress;
		}

		if (!progress)
			return {};

		return progress->GetSnapshot();
	}

	std::shared_ptr<ShaderConfiguration::ConfigurationProgress> GetOperationProgressTracker()
	{
		std::lock_guard<std::mutex> operationLock(gOperationMutex);
		return gOperationProgress;
	}
} //namespace DatabaseShaderConfigurations
