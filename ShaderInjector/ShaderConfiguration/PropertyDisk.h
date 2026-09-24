#pragma once

#include <string>

#include "JsonHelper.h"

namespace ShaderConfiguration
{
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
} //namespace ShaderConfiguration
