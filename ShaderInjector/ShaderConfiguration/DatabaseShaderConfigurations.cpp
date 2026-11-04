#include "DatabaseShaderConfigurations.h"
#include "DatabaseShaderConfigurationsInternal.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "IO/ShaderInjectorIO.h"
#include "ShaderConfiguration/ShaderConfigurationCategories.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"
#include "StringHelper.h"

namespace DatabaseShaderConfigurations
{
	ShaderConfiguration::DocumentDisk gDocument;
	ShaderConfiguration::DocumentDisk gAppliedDocument;
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> gAppliedConfiguration;
	std::recursive_mutex gConfigurationMutex;
	bool gHasAppliedDocument = false;
	bool gLoaded = false;
	std::vector<ShaderConfiguration::PresetDisk> gPresets;

	namespace
	{
		std::string PresetPath(const std::string& presetID)
		{
			const std::string fileName = ShaderConfiguration::GetPresetFileName(presetID);
			if (fileName.empty())
				return {};
			return ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetShaderConfigurationPresetsDirectory(), fileName);
		}

		ShaderConfiguration::PresetDisk MakePreset(const ShaderConfiguration::DocumentDisk& document, const std::string& name)
		{
			ShaderConfiguration::PresetDisk preset{};
			preset.id = document.presetID;
			preset.name = name;
			for (const ShaderConfiguration::PropertyDisk& property : document.properties)
				preset.values[property.id] = property.value;
			return preset;
		}

		void ApplyPresetValues(ShaderConfiguration::DocumentDisk& document, const ShaderConfiguration::PresetDisk& preset)
		{
			document.presetID = preset.id;
			for (ShaderConfiguration::PropertyDisk& property : document.properties)
			{
				const auto value = preset.values.find(property.id);
				std::string selectedValue = property.defaultValue;
				if (value != preset.values.end())
					selectedValue = value->second;
				if (property.valueType.empty())
					property.value = selectedValue;
				else
					property.value = ShaderConfiguration::Internal::NormalizeValue(property.valueType, selectedValue, property.defaultValue, false);
			}
		}

		void LoadPresetCatalog()
		{
			gPresets.clear();
			std::vector<std::string> presetPaths;
			ShaderInjectorIO::CollectFilesByExtension(ShaderInjectorIO::GetShaderConfigurationPresetsDirectory(), ShaderInjectorIO::extensionJSON, presetPaths, false, true);
			std::sort(presetPaths.begin(), presetPaths.end());
			for (const std::string& path : presetPaths)
			{
				ShaderConfiguration::PresetDisk preset;
				if (ShaderConfiguration::LoadPresetJson(path, preset))
					gPresets.push_back(std::move(preset));
			}
		}

		ShaderConfiguration::PresetDisk* FindPreset(const std::string& presetID)
		{
			for (ShaderConfiguration::PresetDisk& preset : gPresets)
			{
				if (preset.id == presetID)
					return &preset;
			}
			return nullptr;
		}

		std::string CreatePresetID(const std::string& name)
		{
			std::string baseID;
			for (const unsigned char character : name)
			{
				if (std::isalnum(character))
					baseID.push_back(static_cast<char>(std::tolower(character)));
				else if (!baseID.empty() && baseID.back() != '_')
					baseID.push_back('_');
			}
			while (!baseID.empty() && baseID.back() == '_')
				baseID.pop_back();
			if (baseID.empty())
				baseID = "preset";
			std::string presetID = baseID;
			unsigned int suffix = 2;
			while (FindPreset(presetID))
				presetID = baseID + "_" + std::to_string(suffix++);
			return presetID;
		}

		bool ApplyCategoryUpdates(const std::map<std::string, std::string>& propertyCategories, std::string& outError)
		{
			const ShaderConfiguration::DocumentDisk previousDocument = gDocument;
			std::map<std::string, std::string> originalSourceText;
			std::map<std::string, std::string> updatedSourceText;
			const std::string modifiedShadersDirectory = ShaderInjectorIO::GetModifiedShadersDirectory();

			for (const auto& propertyCategory : propertyCategories)
			{
				auto property = std::find_if(gDocument.properties.begin(), gDocument.properties.end(), [&propertyCategory](const ShaderConfiguration::PropertyDisk& item) { return item.id == propertyCategory.first; });
				if (property == gDocument.properties.end() || property->propertyType != ShaderConfiguration::PropertyType::Shader || !IsSafeRelativeSourcePath(property->sourcePath))
				{
					outError = "A shader property source could not be updated.";
					return false;
				}

				const std::string sourcePath = ShaderInjectorIO::JoinPath(modifiedShadersDirectory, property->sourcePath);
				if (originalSourceText.find(sourcePath) == originalSourceText.end())
				{
					std::string sourceText;
					if (!ShaderInjectorIO::ReadTextFile(sourcePath, sourceText))
					{
						outError = "Could not read shader source while updating its category.";
						return false;
					}
					originalSourceText[sourcePath] = sourceText;
					updatedSourceText[sourcePath] = std::move(sourceText);
				}

				std::string updatedText;
				if (!ShaderConfiguration::UpdatePropertyCategorySource(updatedSourceText[sourcePath], *property, propertyCategory.second, updatedText, outError))
					return false;
				updatedSourceText[sourcePath] = std::move(updatedText);
			}

			std::map<std::string, std::string> stagedPaths;
			std::map<std::string, std::string> backupPaths;
			for (const auto& sourceText : updatedSourceText)
			{
				const std::string stagedPath = sourceText.first + ".category.pending";
				const std::string backupPath = sourceText.first + ".category.backup";
				ShaderInjectorIO::DeleteFileIfExists(stagedPath);
				ShaderInjectorIO::DeleteFileIfExists(backupPath);
				if (!ShaderInjectorIO::WriteTextFile(stagedPath, sourceText.second))
				{
					for (const auto& staged : stagedPaths)
						ShaderInjectorIO::DeleteFileIfExists(staged.second);
					outError = "Could not stage shader category edits.";
					return false;
				}
				stagedPaths[sourceText.first] = stagedPath;
				backupPaths[sourceText.first] = backupPath;
			}

			std::vector<std::string> backedUpPaths;
			const auto restoreSources = [&]()
			{
				for (auto path = backedUpPaths.rbegin(); path != backedUpPaths.rend(); ++path)
				{
					ShaderInjectorIO::DeleteFileIfExists(*path);
					ShaderInjectorIO::MovePath(backupPaths[*path], *path, true);
				}
				for (const auto& staged : stagedPaths)
					ShaderInjectorIO::DeleteFileIfExists(staged.second);
			};

			for (const auto& sourceText : updatedSourceText)
			{
				const std::string& sourcePath = sourceText.first;
				if (!ShaderInjectorIO::MovePath(sourcePath, backupPaths[sourcePath]))
				{
					restoreSources();
					outError = "Could not stage the original shader source.";
					return false;
				}
				backedUpPaths.push_back(sourcePath);
				if (!ShaderInjectorIO::MovePath(stagedPaths[sourcePath], sourcePath))
				{
					restoreSources();
					outError = "Could not publish shader category edits.";
					return false;
				}
			}

			for (const auto& propertyCategory : propertyCategories)
			{
				auto property = std::find_if(gDocument.properties.begin(), gDocument.properties.end(), [&propertyCategory](const ShaderConfiguration::PropertyDisk& item) { return item.id == propertyCategory.first; });
				property->category = propertyCategory.second;
			}

			if (!SaveCatalog(outError))
			{
				gDocument = previousDocument;
				restoreSources();
				return false;
			}

			for (const std::string& sourcePath : backedUpPaths)
				ShaderInjectorIO::DeleteFileIfExists(backupPaths[sourcePath]);
			return true;
		}
	}

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
			size_t componentLength = std::string::npos;
			if (separator != std::string::npos)
				componentLength = separator - componentStart;
			const std::string component = normalizedPath.substr(componentStart, componentLength);

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
			if (property.propertyType != ShaderConfiguration::PropertyType::Shader)
				continue;
			if (!IsSafeRelativeSourcePath(property.sourcePath) || !ShaderInjectorIO::FileExists(ShaderInjectorIO::JoinPath(modifiedShadersDirectory, property.sourcePath)))
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

	void EnsureLoaded()
	{
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);

		if (gLoaded)
			return;

		const std::string configurationsPath = ShaderInjectorIO::GetShaderConfigurationsPath();
		bool loadedSavedDocument = ShaderConfiguration::LoadJson(configurationsPath, gDocument);
		const bool loadedNewCatalog = loadedSavedDocument;

		if (!loadedSavedDocument)
			loadedSavedDocument = ShaderConfiguration::LoadJson(ShaderInjectorIO::GetLegacyShaderConfigurationsPath(), gDocument);

		if (loadedSavedDocument)
		{
			LoadPresetCatalog();
			if (std::none_of(gDocument.categories.begin(), gDocument.categories.end(), [](const ShaderConfiguration::ShaderConfigurationCategory& category) { return category.name == "Uncategorized"; }))
				gDocument.categories.insert(gDocument.categories.begin(), {"Uncategorized"});

			if (!loadedNewCatalog)
			{
				gDocument.presetID = ShaderConfiguration::defaultPresetID;
				ShaderConfiguration::PresetDisk migratedPreset = MakePreset(gDocument, ShaderConfiguration::defaultPresetName);
				ShaderConfiguration::PresetDisk* existingDefaultPreset = FindPreset(migratedPreset.id);
				if (existingDefaultPreset)
					*existingDefaultPreset = migratedPreset;
				else
					gPresets.push_back(migratedPreset);
				std::string migrationError;
				if (!ShaderConfiguration::WriteCatalogAndPreset(configurationsPath, PresetPath(migratedPreset.id), gDocument, migratedPreset, migrationError))
					ShaderInjectorIO::WriteToLogFileWarning("DatabaseShaderConfigurations->EnsureLoaded: migration could not be saved: " + migrationError);
			}

			if (gPresets.empty())
			{
				ShaderConfiguration::PresetDisk defaultPreset = MakePreset(gDocument, ShaderConfiguration::defaultPresetName);
				gPresets.push_back(defaultPreset);
				std::string ignoredError;
				ShaderConfiguration::WritePresetJson(PresetPath(defaultPreset.id), defaultPreset);
			}

			ShaderConfiguration::PresetDisk* activePreset = FindPreset(gDocument.presetID);
			if (!activePreset)
			{
				activePreset = &gPresets.front();
				gDocument.presetID = activePreset->id;
			}
			ApplyPresetValues(gDocument, *activePreset);
			SortPropertiesBySource(gDocument);
			gAppliedDocument = gDocument;
			gHasAppliedDocument = true;

			if (DocumentSourcesExist(gDocument))
			{
				gAppliedConfiguration = std::make_shared<const ShaderConfiguration::CompileConfiguration>(gAppliedDocument);
				gLoaded = true;
				return;
			}
		}

		if (!ReloadProperties())
		{
			//a failed first scan still leaves callers a stable, empty applied configuration.
			gDocument = {};
			gAppliedDocument = {};
			gAppliedConfiguration = std::make_shared<const ShaderConfiguration::CompileConfiguration>(gAppliedDocument);
			gHasAppliedDocument = true;
			gLoaded = true;
			ShaderInjectorIO::WriteToLogFileError("DatabaseShaderConfigurations->EnsureLoaded: using an empty configuration after loading failed");
		}
	}

	bool ReloadProperties()
	{
		ShaderConfiguration::DocumentDisk previousAppliedDocument;
		bool hasPreviousAppliedDocument = false;

		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			previousAppliedDocument = gDocument;
			hasPreviousAppliedDocument = gHasAppliedDocument;
			previousAppliedDocument.presetID = gAppliedDocument.presetID;
			for (ShaderConfiguration::PropertyDisk& property : previousAppliedDocument.properties)
			{
				const auto appliedProperty = std::find_if(gAppliedDocument.properties.begin(), gAppliedDocument.properties.end(), [&property](const ShaderConfiguration::PropertyDisk& candidate) { return candidate.id == property.id; });
				if (appliedProperty != gAppliedDocument.properties.end())
					property.value = appliedProperty->value;
			}
		}

		const PreparedConfigurationOperation operation = PrepareReloadDocument(
			std::move(previousAppliedDocument),
			hasPreviousAppliedDocument,
			ShaderInjectorIO::GetModifiedShadersDirectory(),
			ShaderInjectorIO::GetShaderConfigurationsPath(),
			{});

		if (!operation.succeeded)
		{
			ShaderInjectorIO::WriteToLogFileError(operation.errorMessage);
			return false;
		}

		{
			std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
			gDocument = operation.document;
			gAppliedDocument = operation.document;
			gAppliedConfiguration = operation.configuration;
			gHasAppliedDocument = true;
			gLoaded = true;
			for (ShaderConfiguration::PresetDisk& preset : gPresets)
			{
				if (preset.id != operation.document.presetID)
					continue;
				for (const ShaderConfiguration::PropertyDisk& property : operation.document.properties)
					preset.values[property.id] = property.value;
			}
			if (!FindPreset(operation.document.presetID))
			{
				ShaderConfiguration::PresetDisk initialPreset;
				if (!ShaderConfiguration::LoadPresetJson(PresetPath(operation.document.presetID), initialPreset))
					initialPreset = MakePreset(operation.document, operation.document.presetID);
				gPresets.push_back(std::move(initialPreset));
			}
		}

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

	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> GetAppliedConfiguration()
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		//workers hold this immutable snapshot while the ui keeps editing gDocument.
		return gAppliedConfiguration;
	}

	ShaderConfiguration::DocumentDisk GetAppliedDocument()
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		return gAppliedDocument;
	}

	const std::vector<ShaderConfiguration::PresetDisk>& GetPresets()
	{
		EnsureLoaded();
		return gPresets;
	}

	std::string GetActivePresetID()
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		return gAppliedDocument.presetID;
	}

	bool SelectPreset(const std::string& presetID, std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		ShaderConfiguration::PresetDisk* preset = FindPreset(presetID);
		if (!preset)
		{
			outError = "The selected preset does not exist.";
			return false;
		}
		ApplyPresetValues(gDocument, *preset);
		outError.clear();
		return true;
	}

	bool SavePreset(std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		ShaderConfiguration::PresetDisk* existingPreset = FindPreset(gDocument.presetID);
		if (!existingPreset)
		{
			outError = "The selected preset does not exist.";
			return false;
		}
		ShaderConfiguration::PresetDisk preset = MakePreset(gDocument, existingPreset->name);
		if (!ShaderConfiguration::WritePresetAtomically(PresetPath(preset.id), preset, outError))
		{
			return false;
		}
		*existingPreset = std::move(preset);
		outError.clear();
		return true;
	}

	bool CreatePreset(const std::string& name, std::string& outError)
	{
		EnsureLoaded();
		if (StringHelper::TrimWhitespace(name).empty())
		{
			outError = "Preset name cannot be empty.";
			return false;
		}
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		ShaderConfiguration::DocumentDisk previousDocument = gDocument;
		const std::string presetID = CreatePresetID(name);
		gDocument.presetID = presetID;
		ShaderConfiguration::PresetDisk preset = MakePreset(gDocument, StringHelper::TrimWhitespace(name));
		if (!ShaderConfiguration::WritePresetAtomically(PresetPath(presetID), preset, outError))
		{
			gDocument = std::move(previousDocument);
			return false;
		}
		gPresets.push_back(std::move(preset));
		outError.clear();
		return true;
	}

	bool RenamePreset(const std::string& presetID, const std::string& newName, std::string& outError)
	{
		EnsureLoaded();
		if (StringHelper::TrimWhitespace(newName).empty())
		{
			outError = "Preset name cannot be empty.";
			return false;
		}
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		ShaderConfiguration::PresetDisk* preset = FindPreset(presetID);
		if (!preset)
		{
			outError = "The selected preset does not exist.";
			return false;
		}
		ShaderConfiguration::PresetDisk renamed = *preset;
		renamed.name = StringHelper::TrimWhitespace(newName);
		if (!ShaderConfiguration::WritePresetAtomically(PresetPath(presetID), renamed, outError))
		{
			return false;
		}
		*preset = std::move(renamed);
		outError.clear();
		return true;
	}

	bool DeletePreset(const std::string& presetID, std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		if (gPresets.size() <= 1)
		{
			outError = "At least one preset must remain.";
			return false;
		}
		if (presetID == gAppliedDocument.presetID)
		{
			outError = "Apply another preset before deleting the currently applied preset.";
			return false;
		}
		auto preset = std::find_if(gPresets.begin(), gPresets.end(), [&presetID](const ShaderConfiguration::PresetDisk& item) { return item.id == presetID; });
		if (preset == gPresets.end())
		{
			outError = "The selected preset does not exist.";
			return false;
		}
		ShaderInjectorIO::DeleteFileIfExists(PresetPath(presetID));
		if (ShaderInjectorIO::PathExists(PresetPath(presetID)))
		{
			outError = "Could not delete the preset file.";
			return false;
		}

		gPresets.erase(preset);
		if (gDocument.presetID == presetID)
			ApplyPresetValues(gDocument, gPresets.front());
		outError.clear();
		return true;
	}

	bool AddCategory(const std::string& name, std::string& outError)
	{
		EnsureLoaded();
		const std::string cleanName = StringHelper::TrimWhitespace(name);
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		if (cleanName.empty())
		{
			outError = "Category name cannot be empty.";
			return false;
		}
		for (const ShaderConfiguration::ShaderConfigurationCategory& category : gDocument.categories)
		{
			if (category.name == cleanName)
			{
				outError = "A category with that name already exists.";
				return false;
			}
		}
		gDocument.categories.push_back({cleanName});
		if (!SaveCatalog(outError))
		{
			gDocument.categories.pop_back();
			return false;
		}

		return true;
	}

	bool RenameCategory(const std::string& oldName, const std::string& newName, std::string& outError)
	{
		EnsureLoaded();
		const std::string cleanName = StringHelper::TrimWhitespace(newName);
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		if (cleanName.empty() || oldName == "Uncategorized")
		{
			outError = "That category cannot be renamed.";
			return false;
		}
		for (const ShaderConfiguration::ShaderConfigurationCategory& category : gDocument.categories)
		{
			if (category.name == cleanName)
			{
				outError = "A category with that name already exists.";
				return false;
			}
		}
		auto category = std::find_if(gDocument.categories.begin(), gDocument.categories.end(), [&oldName](const ShaderConfiguration::ShaderConfigurationCategory& item) { return item.name == oldName; });
		if (category == gDocument.categories.end())
		{
			outError = "The category does not exist.";
			return false;
		}
		const ShaderConfiguration::DocumentDisk previousDocument = gDocument;
		std::map<std::string, std::string> propertyCategories;
		for (ShaderConfiguration::PropertyDisk& property : gDocument.properties)
		{
			if (property.category != oldName)
				continue;
			if (property.propertyType == ShaderConfiguration::PropertyType::Shader)
				propertyCategories[property.id] = cleanName;
			else
				property.category = cleanName;
		}
		category->name = cleanName;
		if (!ApplyCategoryUpdates(propertyCategories, outError))
		{
			gDocument = previousDocument;
			return false;
		}

		return true;
	}

	bool RemoveCategory(const std::string& name, std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		if (name == "Uncategorized")
		{
			outError = "The Uncategorized category is required.";
			return false;
		}
		auto category = std::find_if(gDocument.categories.begin(), gDocument.categories.end(), [&name](const ShaderConfiguration::ShaderConfigurationCategory& item) { return item.name == name; });
		if (category == gDocument.categories.end())
		{
			outError = "The category does not exist.";
			return false;
		}
		const ShaderConfiguration::DocumentDisk previousDocument = gDocument;
		std::map<std::string, std::string> propertyCategories;
		for (ShaderConfiguration::PropertyDisk& property : gDocument.properties)
		{
			if (property.category != name)
				continue;
			if (property.propertyType == ShaderConfiguration::PropertyType::Shader)
				propertyCategories[property.id] = "Uncategorized";
			else
				property.category = "Uncategorized";
		}
		gDocument.categories.erase(category);
		if (!ApplyCategoryUpdates(propertyCategories, outError))
		{
			gDocument = previousDocument;
			return false;
		}

		return true;
	}

	bool SetPropertyCategory(const std::string& propertyID, const std::string& categoryName, std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		const bool categoryExists = categoryName == "Uncategorized" || std::any_of(gDocument.categories.begin(), gDocument.categories.end(), [&categoryName](const ShaderConfiguration::ShaderConfigurationCategory& category) { return category.name == categoryName; });
		if (!categoryExists)
		{
			outError = "The category does not exist.";
			return false;
		}
		auto property = std::find_if(gDocument.properties.begin(), gDocument.properties.end(), [&propertyID](const ShaderConfiguration::PropertyDisk& item) { return item.id == propertyID; });
		if (property == gDocument.properties.end())
		{
			outError = "The property does not exist.";
			return false;
		}
		const ShaderConfiguration::DocumentDisk previousDocument = gDocument;
		if (property->propertyType == ShaderConfiguration::PropertyType::Shader)
		{
			const std::map<std::string, std::string> propertyCategories = {{propertyID, categoryName}};
			return ApplyCategoryUpdates(propertyCategories, outError);
		}

		property->category = categoryName;
		if (!SaveCatalog(outError))
		{
			gDocument = previousDocument;
			return false;
		}

		return true;
	}

	bool AddRenderPassProperty(const std::string& name, const std::string& renderPassID, ShaderConfiguration::RenderPassPropertyType propertyType, const std::string& defaultValue, const std::string& description, const std::string& categoryName, std::string& outError)
	{
		return AddRenderPassProperty(name, std::vector<std::string>{renderPassID}, propertyType, defaultValue, description, categoryName, outError);
	}

	bool AddRenderPassProperty(const std::string& name, const std::vector<std::string>& renderPassIDs, ShaderConfiguration::RenderPassPropertyType propertyType, const std::string& defaultValue, const std::string& description, const std::string& categoryName, std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		const bool categoryExists = categoryName == "Uncategorized" || std::any_of(gDocument.categories.begin(), gDocument.categories.end(), [&categoryName](const ShaderConfiguration::ShaderConfigurationCategory& category) { return category.name == categoryName; });
		if (name.empty() || renderPassIDs.empty() || !categoryExists)
		{
			outError = "The custom render-pass property is missing a name, binding, or category.";
			return false;
		}
		if (!ShaderConfiguration::IsEnabledRenderPassProperty(propertyType) && !ShaderConfiguration::IsDownscaleRenderPassProperty(propertyType))
		{
			outError = "The render-pass property binding is unsupported.";
			return false;
		}

		std::vector<std::string> normalizedRenderPassIDs = renderPassIDs;
		std::sort(normalizedRenderPassIDs.begin(), normalizedRenderPassIDs.end());
		if (normalizedRenderPassIDs.front().empty() || std::adjacent_find(normalizedRenderPassIDs.begin(), normalizedRenderPassIDs.end()) != normalizedRenderPassIDs.end())
		{
			outError = "Render-pass targets must be non-empty and unique.";
			return false;
		}
		if (ShaderConfiguration::IsMultipleRenderPassProperty(propertyType) != (normalizedRenderPassIDs.size() > 1))
		{
			outError = "Single render-pass properties require one target and multiple properties require more than one target.";
			return false;
		}

		for (const ShaderConfiguration::PropertyDisk& existingProperty : gDocument.properties)
		{
			if (existingProperty.propertyType != ShaderConfiguration::PropertyType::RenderPass)
				continue;

			const bool sameSetting = ShaderConfiguration::IsEnabledRenderPassProperty(existingProperty.renderPassPropertyType) == ShaderConfiguration::IsEnabledRenderPassProperty(propertyType) &&
				ShaderConfiguration::IsDownscaleRenderPassProperty(existingProperty.renderPassPropertyType) == ShaderConfiguration::IsDownscaleRenderPassProperty(propertyType);
			if (!sameSetting)
				continue;

			std::vector<std::string> existingIDs = existingProperty.renderPassIDs;
			if (existingIDs.empty() && !existingProperty.renderPassID.empty())
				existingIDs.push_back(existingProperty.renderPassID);
			std::sort(existingIDs.begin(), existingIDs.end());
			std::vector<std::string> overlappingIDs;
			std::set_intersection(existingIDs.begin(), existingIDs.end(), normalizedRenderPassIDs.begin(), normalizedRenderPassIDs.end(), std::back_inserter(overlappingIDs));
			if (!overlappingIDs.empty())
			{
				outError = "A property already targets one of those render-pass settings.";
				return false;
			}
		}

		ShaderConfiguration::PropertyDisk property{};
		property.propertyType = ShaderConfiguration::PropertyType::RenderPass;
		property.renderPassPropertyType = propertyType;
		property.renderPassIDs = normalizedRenderPassIDs;
		property.renderPassID = normalizedRenderPassIDs.front();
		property.id = "RenderPass::";
		if (ShaderConfiguration::IsMultipleRenderPassProperty(propertyType))
			property.id += "Multiple::";
		else
			property.id += normalizedRenderPassIDs.front() + "::";
		if (ShaderConfiguration::IsEnabledRenderPassProperty(propertyType))
			property.id += "Enabled";
		else
			property.id += "DownscalePowerOfTwo";
		if (ShaderConfiguration::IsMultipleRenderPassProperty(propertyType))
			for (const std::string& targetID : normalizedRenderPassIDs)
				property.id += "::" + std::to_string(targetID.size()) + ":" + targetID;

		property.name = name;
		property.description = description;
		property.category = categoryName;
		property.valueType = "int";
		if (ShaderConfiguration::IsEnabledRenderPassProperty(propertyType))
			property.valueType = "bool";

		property.value = defaultValue;
		if (property.valueType == "bool")
		{
			bool enabled = false;
			if (!ShaderConfiguration::TryGetBoolean(property, enabled))
			{
				outError = "The enabled render pass property requires a true or false default.";
				return false;
			}
			property.defaultValue = "false";
			if (enabled)
				property.defaultValue = "true";
		}
		else
		{
			int factor = 0;
			if (!ShaderConfiguration::TryGetInteger(property, factor) || factor < 1 || factor > 1024 || (factor & (factor - 1)) != 0)
			{
				outError = "The downscale render pass property default must be a power of two from 1 through 1024.";
				return false;
			}
			property.defaultValue = std::to_string(factor);
		}

		property.value = property.defaultValue;
		gDocument.properties.push_back(property);
		if (!SaveCatalog(outError))
		{
			gDocument.properties.pop_back();
			return false;
		}
		return SavePreset(outError);
	}
	bool RemoveProperty(const std::string& propertyID, std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		auto property = std::find_if(gDocument.properties.begin(), gDocument.properties.end(), [&propertyID](const ShaderConfiguration::PropertyDisk& item) { return item.id == propertyID; });
		if (property == gDocument.properties.end() || property->propertyType != ShaderConfiguration::PropertyType::RenderPass)
		{
			outError = "Only custom render-pass properties can be removed.";
			return false;
		}
		const ShaderConfiguration::PropertyDisk removedProperty = *property;
		gDocument.properties.erase(property);
		if (!SaveCatalog(outError))
		{
			gDocument.properties.push_back(removedProperty);
			return false;
		}
		for (ShaderConfiguration::PresetDisk& preset : gPresets)
		{
			preset.values.erase(propertyID);
			if (!ShaderConfiguration::WritePresetJson(PresetPath(preset.id), preset))
			{
				outError = "The property was removed, but a preset value could not be updated.";
				return false;
			}
		}
		return true;
	}

	bool SaveCatalog(std::string& outError)
	{
		EnsureLoaded();
		std::lock_guard<std::recursive_mutex> configurationLock(gConfigurationMutex);
		ShaderConfiguration::DocumentDisk catalog = gDocument;
		catalog.presetID = gAppliedDocument.presetID;
		catalog.schemaVersion = ShaderConfiguration::currentSchemaVersion;
		if (!ShaderConfiguration::WriteJson(ShaderInjectorIO::GetShaderConfigurationsPath(), catalog))
		{
			outError = "Could not save the configuration catalog.";
			return false;
		}
		outError.clear();
		return true;
	}

} //namespace DatabaseShaderConfigurations
