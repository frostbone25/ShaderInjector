#pragma once

#include <string>
#include <vector>

#include "JsonHelper.h"
#include "ShaderConfiguration/PropertyType.h"
#include "ShaderConfiguration/RenderPassPropertyType.h"

namespace ShaderConfiguration
{
	class ShaderConfigurationProperty
	{
	public:
		std::string id;
		std::string name;
		std::string sourceFile;
		std::string sourcePath;
		std::string defaultValue;
		std::string value;
		bool booleanUsesDefinitionPresence = false;
		int definitionIndex = 0;
		int sourceOrder = 0;
		std::string valueType;
		std::string description;
		std::string valueRange;
		PropertyType propertyType = PropertyType::Shader;
		std::string category = "Uncategorized";
		std::string renderPassID;
		std::vector<std::string> renderPassIDs;
		RenderPassPropertyType renderPassPropertyType = RenderPassPropertyType::Enabled;
	};

	inline void to_json(nlohmann::ordered_json& json, const ShaderConfigurationProperty& property)
	{
		json = nlohmann::ordered_json{
			{"id", property.id},
			{"name", property.name},
			{"sourceFile", property.sourceFile},
			{"sourcePath", property.sourcePath},
			{"defaultValue", property.defaultValue},
			{"booleanUsesDefinitionPresence", property.booleanUsesDefinitionPresence},
			{"definitionIndex", property.definitionIndex},
			{"sourceOrder", property.sourceOrder},
			{"valueType", property.valueType},
			{"description", property.description},
			{"valueRange", property.valueRange},
			{"propertyType", static_cast<int>(property.propertyType)},
			{"category", property.category},
			{"renderPassID", property.renderPassID},
			{"renderPassIDs", property.renderPassIDs},
			{"renderPassPropertyType", static_cast<int>(property.renderPassPropertyType)}};
	}

	inline void from_json(const nlohmann::ordered_json& json, ShaderConfigurationProperty& property)
	{
		property.id = json.value("id", std::string());
		property.name = json.value("name", std::string());
		property.sourceFile = json.value("sourceFile", std::string());
		property.sourcePath = json.value("sourcePath", std::string());
		property.defaultValue = json.value("defaultValue", std::string());
		property.value = json.value("value", property.defaultValue);
		property.booleanUsesDefinitionPresence = json.value("booleanUsesDefinitionPresence", false);
		property.definitionIndex = json.value("definitionIndex", 0);
		property.sourceOrder = json.value("sourceOrder", 0);
		property.valueType = json.value("valueType", json.value("type", std::string()));
		property.description = json.value("description", json.value("comment", std::string()));
		property.valueRange = json.value("valueRange", json.value("range", std::string()));
		property.category = json.value("category", std::string("Uncategorized"));
		property.renderPassID = json.value("renderPassID", std::string());
		property.renderPassIDs = json.value("renderPassIDs", std::vector<std::string>());
		if (property.renderPassIDs.empty() && !property.renderPassID.empty())
			property.renderPassIDs.push_back(property.renderPassID);
		const int propertyType = json.value("propertyType", 0);
		if (propertyType >= static_cast<int>(PropertyType::Shader) && propertyType <= static_cast<int>(PropertyType::RenderPass))
			property.propertyType = static_cast<PropertyType>(propertyType);

		const int renderPassPropertyType = json.value("renderPassPropertyType", 0);
		if (renderPassPropertyType >= static_cast<int>(RenderPassPropertyType::Enabled) && renderPassPropertyType <= static_cast<int>(RenderPassPropertyType::DownscalePowerOfTwoMultiple))
			property.renderPassPropertyType = static_cast<RenderPassPropertyType>(renderPassPropertyType);
	}
} //namespace ShaderConfiguration
