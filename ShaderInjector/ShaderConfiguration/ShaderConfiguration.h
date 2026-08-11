#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "JsonHelper.h"

namespace ShaderConfiguration
{
	inline constexpr const char* formatName = "ShaderInjector.ShaderConfigurations";
	inline constexpr int currentSchemaVersion = 3;

	struct PropertyDisk
	{
		std::string id;
		std::string name;
		std::string sourceFile;
		std::string sourcePath;
		std::string comment;
		std::string type;
		std::string defaultValue;
		std::string value;
		std::string range;
		bool booleanUsesDefinitionPresence = false;
		int definitionIndex = 0;
		int sourceOrder = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			PropertyDisk,
			id,
			name,
			sourceFile,
			sourcePath,
			comment,
			type,
			defaultValue,
			value,
			range,
			booleanUsesDefinitionPresence,
			definitionIndex,
			sourceOrder)
	};

	struct DocumentDisk
	{
		int schemaVersion = currentSchemaVersion;
		std::string format = formatName;
		std::vector<PropertyDisk> properties;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			DocumentDisk,
			schemaVersion,
			format,
			properties)
	};

	bool WriteJson(const std::string& path, const DocumentDisk& document);
	bool LoadJson(const std::string& path, DocumentDisk& outDocument);

	bool ParseShaderSource(
		const std::string& relativeSourcePath,
		const std::string& sourceText,
		std::vector<PropertyDisk>& outProperties);

	bool RewriteShaderSource(
		const std::string& sourceText,
		const std::vector<const PropertyDisk*>& properties,
		std::string& outSourceText,
		std::string& outError);

	bool TryGetBoolean(const PropertyDisk& property, bool& outValue);
	bool TryGetInteger(const PropertyDisk& property, int& outValue);
	bool TryGetFloatComponents(const PropertyDisk& property, std::vector<float>& outValues);
	bool TryGetRange(const PropertyDisk& property, float& outMinimum, float& outMaximum);

	void SetBoolean(PropertyDisk& property, bool value);
	void SetInteger(PropertyDisk& property, int value);
	void SetFloatComponents(PropertyDisk& property, const float* values, size_t componentCount);
	size_t ComponentCount(const PropertyDisk& property);
}
