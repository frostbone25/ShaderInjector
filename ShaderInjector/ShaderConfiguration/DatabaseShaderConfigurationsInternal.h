#pragma once

#include <string>

#include "ShaderConfiguration/DocumentDisk.h"

namespace DatabaseShaderConfigurations
{
	extern ShaderConfiguration::DocumentDisk gDocument;
	bool IsSafeRelativeSourcePath(const std::string& sourcePath);
}
