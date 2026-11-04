#pragma once

#include <map>
#include <string>

#include "JsonHelper.h"

namespace ShaderConfiguration
{
	struct PresetDisk
	{
		std::string id;
		std::string name;
		std::map<std::string, std::string> values;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PresetDisk, id, name, values)
	};
} //namespace ShaderConfiguration
