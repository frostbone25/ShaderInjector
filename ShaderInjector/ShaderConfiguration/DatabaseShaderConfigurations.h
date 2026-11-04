#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ShaderConfiguration/ApplyResult.h"
#include "ShaderConfiguration/CompileConfiguration.h"
#include "ShaderConfiguration/ConfigurationProgressSnapshot.h"
#include "ShaderConfiguration/ConfigurationOperationResult.h"
#include "ShaderConfiguration/ShaderConfiguration.h"
#include "ShaderConfiguration/PresetDisk.h"

namespace ShaderConfiguration
{
	class ConfigurationProgress;
}

namespace DatabaseShaderConfigurations
{
	void EnsureLoaded();
	//startup calls this on its worker before loading cached shader variants.
	void PrepareStartupConfiguration();
	bool ReloadProperties();

	const ShaderConfiguration::DocumentDisk& GetDocument();
	ShaderConfiguration::DocumentDisk& GetEditableDocument();
	ShaderConfiguration::DocumentDisk GetAppliedDocument();
	const std::vector<ShaderConfiguration::PresetDisk>& GetPresets();
	std::string GetActivePresetID();
	bool SelectPreset(const std::string& presetID, std::string& outError);
	bool SavePreset(std::string& outError);
	bool CreatePreset(const std::string& name, std::string& outError);
	bool RenamePreset(const std::string& presetID, const std::string& newName, std::string& outError);
	bool DeletePreset(const std::string& presetID, std::string& outError);
	bool AddCategory(const std::string& name, std::string& outError);
	bool RenameCategory(const std::string& oldName, const std::string& newName, std::string& outError);
	bool RemoveCategory(const std::string& name, std::string& outError);
	bool SetPropertyCategory(const std::string& propertyID, const std::string& category, std::string& outError);
	bool AddRenderPassProperty(
		const std::string& name,
		const std::string& renderPassID,
		ShaderConfiguration::RenderPassPropertyType propertyType,
		const std::string& defaultValue,
		const std::string& description,
		const std::string& category,
		std::string& outError);
	bool AddRenderPassProperty(
		const std::string& name,
		const std::vector<std::string>& renderPassIDs,
		ShaderConfiguration::RenderPassPropertyType propertyType,
		const std::string& defaultValue,
		const std::string& description,
		const std::string& category,
		std::string& outError);
	bool RemoveProperty(const std::string& propertyID, std::string& outError);
	bool SaveCatalog(std::string& outError);
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> GetAppliedConfiguration();
	bool CancelBackgroundCompilation();
	bool QueueApplyChanges(bool forceRecompileAll = false);
	bool QueueReloadProperties();
	ConfigurationOperationResult ProcessCompletedOperation();
	ShaderConfiguration::ConfigurationProgressSnapshot GetOperationProgress();
	std::shared_ptr<ShaderConfiguration::ConfigurationProgress> GetOperationProgressTracker();

	ApplyResult ApplyChanges();
} //namespace DatabaseShaderConfigurations
