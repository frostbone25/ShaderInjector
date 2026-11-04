#pragma once

#include <string>

namespace ShaderConfiguration
{
	struct PropertyMacro
	{
		std::string sourcePath;
		std::string name;
		std::string id;
		std::string alias;
		std::string value;
		bool usesDefinitionPresence = false;
		int definitionIndex = 0;
	};
} //namespace ShaderConfiguration
