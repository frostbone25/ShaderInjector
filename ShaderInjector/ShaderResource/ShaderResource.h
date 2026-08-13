#pragma once

#include <string>

namespace ShaderResource
{
	struct TextureDisk
	{
		// Portable path relative to ShaderInjector/ShaderResources.
		std::string id;
		std::string name;
		std::string fileName;
		std::string filePath;
	};
}
