#pragma once

#include <string>

namespace ShaderConfiguration::Internal
{
	struct ParsedDefine
	{
		std::string name;
		std::string value;
		bool commentedOut = false;
	};
}
