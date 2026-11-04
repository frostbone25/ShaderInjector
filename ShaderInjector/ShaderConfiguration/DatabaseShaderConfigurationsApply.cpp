#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/DatabaseShaderConfigurationsInternal.h"

#include <mutex>
#include <utility>

#include "IO/ShaderInjectorIO.h"

namespace DatabaseShaderConfigurations
{
	ApplyResult ApplyChanges()
	{
		EnsureLoaded();
		ShaderConfiguration::DocumentDisk editableDocument;

		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			editableDocument = gDocument;
		}

		const PreparedConfigurationOperation operation = PrepareApplyDocument(
			std::move(editableDocument),
			ShaderInjectorIO::GetModifiedShadersDirectory(),
			ShaderInjectorIO::GetShaderConfigurationsPath(),
			{});

		if (operation.succeeded)
		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			gAppliedDocument = operation.document;
			gAppliedConfiguration = operation.configuration;
			gHasAppliedDocument = true;
			gDocument = operation.document;
			for (ShaderConfiguration::PresetDisk& preset : gPresets)
			{
				if (preset.id != operation.document.presetID)
					continue;
				for (const ShaderConfiguration::PropertyDisk& property : operation.document.properties)
					preset.values[property.id] = property.value;
			}
			ShaderInjectorIO::WriteToLogFileSuccess("DatabaseShaderConfigurations->ApplyChanges: properties = " + std::to_string(operation.applyResult.propertyCount) + " sources = " + std::to_string(operation.applyResult.sourceFileCount));
		}

		return operation.applyResult;
	}
} //namespace DatabaseShaderConfigurations
