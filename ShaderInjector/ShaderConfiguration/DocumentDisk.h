#pragma once

#include <string>
#include <vector>

#include "JsonHelper.h"
#include "ShaderConfiguration/PropertyDisk.h"
#include "ShaderConfiguration/ShaderConfigurationCategory.h"
#include "ShaderConfiguration/ShaderConfigurationConstants.h"

namespace ShaderConfiguration
{
	struct DocumentDisk
	{
		int schemaVersion = currentSchemaVersion;
		std::string format = formatName;
		std::string presetID = defaultPresetID;
		std::vector<ShaderConfigurationCategory> categories;
		std::vector<PropertyDisk> properties;

	};

	inline void to_json(nlohmann::ordered_json& json, const DocumentDisk& document)
	{
		json = nlohmann::ordered_json{
			{"schemaVersion", document.schemaVersion},
			{"format", document.format},
			{"activePresetID", document.presetID},
			{"categories", document.categories},
			{"properties", document.properties}};
	}

	inline void from_json(const nlohmann::ordered_json& json, DocumentDisk& document)
	{
		document.schemaVersion = json.value("schemaVersion", 1);
		document.format = json.value("format", std::string());
		document.presetID = json.value("activePresetID", std::string(defaultPresetID));
		document.categories = json.value("categories", std::vector<ShaderConfigurationCategory>());
		document.properties = json.value("properties", std::vector<PropertyDisk>());
	}
} //namespace ShaderConfiguration
