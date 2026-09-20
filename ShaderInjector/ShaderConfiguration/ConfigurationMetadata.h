#pragma once

#include <string>
#include <vector>

namespace ShaderConfiguration::Internal
{
	struct ConfigurationMetadata
	{
		bool noConfiguration = false;
		bool hasConfigurationTag = false;
		std::string type;
		std::string defaultValue;
		std::string range;
		std::vector<std::string> descriptionLines;
	};
}
