#pragma once

#include <string>

#include "JsonHelper.h"

namespace ShaderConfiguration
{
	class ShaderConfigurationCategory
	{
	public:
		std::string name;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShaderConfigurationCategory, name)
	};
} //namespace ShaderConfiguration
