#pragma once

#include <string>
#include <vector>

#include "JsonHelper.h"
#include "ShaderConfiguration/PropertyDisk.h"
#include "ShaderConfiguration/ShaderConfigurationConstants.h"

namespace ShaderConfiguration
{
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
}
